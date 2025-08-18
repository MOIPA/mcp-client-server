#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <atomic>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib_old.h"
#include "nlohmann/json.hpp"

// for convenience
using json = nlohmann::json;

const std::string EXTERNAL_MCP_SSE_URL = "https://web-mcp.koyeb.app/sse/e86f79f2-f0c4-40e1-ae33-b701bb3959a7";
const std::string EXTERNAL_MCP_BASE_URL = "https://web-mcp.koyeb.app";
const std::string LLM_SERVICE_URL = "http://localhost:8080";
const std::string PROXY_HOST = "proxyhk.zte.com.cn";
const int PROXY_PORT = 80;

struct SharedState {
    std::mutex mtx;
    std::condition_variable cv;
    std::string session_path;
    bool is_session_ready = false;
    std::map<int, json> responses;
    std::atomic<bool> has_error{false};
    std::string error_message;
};

json create_json_rpc_body(const std::string& method, const json& params, int id) {
    json rpc_body;
    rpc_body["jsonrpc"] = "2.0";
    rpc_body["id"] = id;
    rpc_body["method"] = method;
    rpc_body["params"] = params;
    return rpc_body;
}

void sse_worker_thread(SharedState& state) {
    httplib::Client sse_client(EXTERNAL_MCP_BASE_URL.c_str());
    sse_client.set_proxy(PROXY_HOST.c_str(), PROXY_PORT);
    sse_client.enable_server_certificate_verification(false);
    // sse_client.set_version("HTTP/1.1"); // Force HTTP/1.1
    sse_client.set_read_timeout(0);

    httplib::Headers headers;
    auto res = sse_client.Get(EXTERNAL_MCP_SSE_URL.c_str(), headers, 
        [&](const char *data, size_t data_length) {
            std::string chunk(data, data_length);
            if (chunk.find("event: endpoint") != std::string::npos) {
                size_t data_pos = chunk.find("data: ");
                if (data_pos != std::string::npos) {
                    std::unique_lock<std::mutex> lock(state.mtx);
                    state.session_path = chunk.substr(data_pos + 6);
                    if (!state.session_path.empty() && state.session_path.back() == '\n') {
                        state.session_path.pop_back();
                    }
                    state.is_session_ready = true;
                    lock.unlock();
                    state.cv.notify_all();
                }
            } else if (chunk.find("event: message") != std::string::npos) {
                size_t data_pos = chunk.find("data: ");
                if (data_pos != std::string::npos) {
                    try {
                        json rpc_response = json::parse(chunk.substr(data_pos + 6));
                        if (rpc_response.contains("id")) {
                            int response_id = rpc_response["id"].get<int>();
                            std::unique_lock<std::mutex> lock(state.mtx);
                            state.responses[response_id] = rpc_response;
                            lock.unlock();
                            state.cv.notify_all();
                        }
                    } catch (...) {}
                }
            }
            return true;
        });

    if (!res || res->status != 200) {
        std::unique_lock<std::mutex> lock(state.mtx);
        state.has_error = true;
        state.error_message = "Failed to connect to SSE endpoint. Status: " + std::to_string(res ? res->status : -1) + ", Error: " + httplib::to_string(res.error());
        lock.unlock();
        state.cv.notify_all();
    }
}

json extract_json_from_response(const std::string& text) {
    size_t start = text.rfind("{");
    if (start == std::string::npos) throw std::runtime_error("Could not find start of JSON");
    size_t end = text.rfind("}");
    if (end == std::string::npos || end < start) throw std::runtime_error("Could not find end of JSON");
    std::string json_str = text.substr(start, end - start + 1);
    try {
        return json::parse(json_str);
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Failed to parse JSON: " + std::string(e.what()));
    }
}

int main() {
    SharedState state;
    std::thread sse_thread(sse_worker_thread, std::ref(state));
    sse_thread.detach();

    std::cout << "[Agent] Establishing session..." << std::endl;
    {
        std::unique_lock<std::mutex> lock(state.mtx);
        state.cv.wait(lock, [&]{ return state.is_session_ready || state.has_error; });
        if (state.has_error) {
            std::cerr << "[Agent] Fatal Error: " << state.error_message << std::endl;
            return 1;
        }
        std::cout << "[Agent] Session established! Path: " << state.session_path << std::endl;
    }

    httplib::Client mcp_client(EXTERNAL_MCP_BASE_URL.c_str());
    mcp_client.set_proxy(PROXY_HOST.c_str(), PROXY_PORT);
    mcp_client.enable_server_certificate_verification(false);

    httplib::Client llm_client(LLM_SERVICE_URL.c_str());
    llm_client.set_connection_timeout(600);

    json available_tools;
    int tools_list_id = 1;
    {
        json rpc_body = create_json_rpc_body("tools/list", {{"_meta", {{"progressToken", 1}}}}, tools_list_id);
        httplib::Headers headers;
        auto res = mcp_client.Post(state.session_path.c_str(), headers, rpc_body.dump(), "application/json");
        if (!res || res->status != 200) { std::cerr << "[Agent] Fatal: Failed to send tools/list request. Status: " << (res ? res->status : -1) << ", Error: " << httplib::to_string(res.error()) << std::endl; return 1; }

        std::cout << "[Agent] Discovering tools..." << std::endl;
        std::unique_lock<std::mutex> lock(state.mtx);
        state.cv.wait(lock, [&]{ return state.responses.count(tools_list_id) > 0 || state.has_error; });
        if (state.has_error) { std::cerr << "[Agent] Fatal Error: " << state.error_message << std::endl; return 1; }

        json tool_list_response = state.responses[tools_list_id];
        available_tools = tool_list_response["result"]["tools"];
        state.responses.erase(tools_list_id);
        std::cout << "[Agent] Found " << available_tools.size() << " tools." << std::endl;
    }

    std::cout << "\n=========================================================" << std::endl;
    std::cout << "      Browser Automation Agent (Definitive Version)    " << std::endl;
    std::cout << "=========================================================" << std::endl;

    int request_id_counter = 2;

    while (true) {
        std::cout << "\nYOU: ";
        std::string user_prompt;
        std::getline(std::cin, user_prompt);

        if (user_prompt == "exit" || user_prompt == "quit") break;
        if (user_prompt.empty()) continue;

        std::string prompt_for_tool_selection = 
            "You are a browser automation agent. Choose a tool. Respond with ONLY a JSON object specifying 'tool_name' and 'parameters'.\n"
            "--- USER REQUEST ---\n" + user_prompt + "\n\n"
            "--- AVAILABLE TOOLS ---\n" + available_tools.dump(2) + "\n\n"
            "Respond with JSON only.";

        json llm_req_body = {{"model", "my-llm"}, {"messages", prompt_for_tool_selection}};
        httplib::Headers llm_headers;
        auto llm_res = llm_client.Post("/v1/chat/completions", llm_headers, llm_req_body.dump(), "application/json");
        if (!llm_res || llm_res->status != 200) { std::cerr << "[Agent] Error: LLM service failed." << std::endl; continue; }
        
        json llm_response = json::parse(llm_res->body);
        std::string llm_content = llm_response["choices"][0]["message"]["content"];
        
        json tool_call;
        try {
            tool_call = extract_json_from_response(llm_content);
        } catch (const std::exception& e) {
            std::cerr << "[Agent] Error: Could not parse tool call. " << e.what() << std::endl;
            continue;
        }

        std::string tool_method = tool_call.value("tool_name", "");
        json tool_params = tool_call.value("parameters", json::object());
        int current_request_id = request_id_counter++;

        std::cout << "[Agent] Executing '" << tool_method << "' (ID: " << current_request_id << ") అధ్యక్ష..." << std::endl;
        json rpc_body = create_json_rpc_body(tool_method, tool_params, current_request_id);
        httplib::Headers tool_headers;
        auto tool_res = mcp_client.Post(state.session_path.c_str(), tool_headers, rpc_body.dump(), "application/json");
        if (!tool_res || tool_res->status != 200) { std::cerr << "[Agent] Error: Tool POST failed. Status: " << (tool_res ? tool_res->status : -1) << ", Error: " << httplib::to_string(tool_res.error()) << std::endl; continue; }

        std::cout << "[Agent] Command sent. Waiting for result..." << std::endl;
        json final_tool_result;
        {
            std::unique_lock<std::mutex> lock(state.mtx);
            state.cv.wait(lock, [&]{ return state.responses.count(current_request_id) > 0 || state.has_error; });
            if (state.has_error) { std::cerr << "[Agent] Fatal Error: " << state.error_message << std::endl; break; }

            json tool_response = state.responses[current_request_id];
            state.responses.erase(current_request_id);
            if (tool_response.contains("error")) {
                std::cerr << "[Agent] MCP server error: " << tool_response["error"]["message"].get<std::string>() << std::endl;
                continue;
            }
            final_tool_result = tool_response["result"];
        }
        std::cout << "[Agent] Result received!" << std::endl;

        std::string prompt_for_final_answer =
            "Based on the user request and the tool result, formulate a friendly response.\n\n"
            "--- USER REQUEST ---\n"
            + user_prompt + "\n\n"
            "--- TOOL RESULT ---\n"
            + final_tool_result.dump(2) + "\n\n"
            "Provide a final response.";

        llm_req_body["messages"] = prompt_for_final_answer;
        auto llm_res2 = llm_client.Post("/v1/chat/completions", llm_headers, llm_req_body.dump(), "application/json");
        if (!llm_res2 || llm_res2->status != 200) { std::cerr << "[Agent] Error: LLM service failed on final response." << std::endl; continue; }
        
        json llm_response2 = json::parse(llm_res2->body);
        std::string final_answer = llm_response2["choices"][0]["message"]["content"];

        std::cout << "\nAGENT: " << final_answer << std::endl;
    }

    std::cout << "[Agent] Goodbye!" << std::endl;
    return 0;
}