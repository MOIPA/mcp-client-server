#include <iostream>
#include <string>
#include <vector>
#include "httplib.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

const std::string MCP_SERVICE_URL = "http://localhost:8081";
const std::string LLM_SERVICE_URL = "http://localhost:8080";

// 从LLM的回答中提取JSON对象
json extract_json_from_response(const std::string& text) {
    size_t start = 0;
    
    // First, try to find a markdown block
    size_t markdown_start = text.find("```json");
    if (markdown_start != std::string::npos) {
        start = markdown_start + 7;
    } else {
        // If no markdown, try to find the end of a <think> block
        size_t think_end = text.rfind("</think>");
        if (think_end != std::string::npos) {
            // Search for the first '{' after the think block
            start = text.find("{", think_end);
        } else {
            // As a last resort, find the last '{' in the entire string
            start = text.rfind("{");
        }
    }

    if (start == std::string::npos) {
        throw std::runtime_error("Could not find start of JSON ('{') in LLM response");
    }

    size_t end = text.rfind("}");
    if (end == std::string::npos || end < start) {
        throw std::runtime_error("Could not find end of JSON ('}') after start");
    }

    std::string json_str = text.substr(start, end - start + 1);
    try {
        return json::parse(json_str);
    } catch (const json::parse_error& e) {
        // Provide more context in the error
        throw std::runtime_error("Failed to parse JSON: " + std::string(e.what()) + "\nExtracted string was: " + json_str);
    }
}

int main(int argc, char* argv[]) {
    // 创建HTTP客户端访问MCP和LLM的服务
    httplib::Client mcp_client(MCP_SERVICE_URL);
    httplib::Client llm_client(LLM_SERVICE_URL);
    mcp_client.set_connection_timeout(5);
    llm_client.set_connection_timeout(600); // LLM can be slow

    std::cout << "=========================================================" << std::endl;
    std::cout << "            MCP Agent (Conversational)                 " << std::endl;
    std::cout << "=========================================================" << std::endl;
    std::cout << "Enter your command, or type 'exit' or 'quit' to leave." << std::endl;

    while (true) {
        std::cout << "\nYOU: ";
        std::string user_prompt;
        std::getline(std::cin, user_prompt);

        if (user_prompt == "exit" || user_prompt == "quit") {
            break;
        }
        if (user_prompt.empty()) {
            continue;
        }

        // 1. 读取MCP服务，获得可用的工具列表
        std::cout << "[Agent] Getting available tools..." << std::endl;
        auto mcp_res = mcp_client.Get("/help");
        if (!mcp_res || mcp_res->status != 200) {
            std::cerr << "[Agent] Error: Could not get tools from MCP service. Is it running?" << std::endl;
            continue;
        }
        json tools_json = json::parse(mcp_res->body);
        // 2. 组合提示词
        std::string prompt_for_tool_selection = 
            "You are an agent that can use tools to answer user requests.\n"
            "Based on the user's request and the available tools, decide which tool to use.\n"
            "Respond with ONLY a JSON object specifying the 'tool_name' and 'parameters'.\n\n"
            "--- USER REQUEST ---\n"
            + user_prompt + "\n\n"
            "--- AVAILABLE TOOLS ---\n"
            + tools_json.dump(2) + "\n\n"
            "Which tool should I use? Respond with JSON only.";

        // 3. 组合提示词请求并发送给LLM，获得工具选择结果
        std::cout << "[Agent] Asking LLM to choose a tool..." << std::endl;
        json llm_req_body;
        llm_req_body["model"] = "my-llm";
        llm_req_body["messages"] = prompt_for_tool_selection;
        
        auto llm_res1 = llm_client.Post("/v1/chat/completions", llm_req_body.dump(), "application/json");
        if (!llm_res1 || llm_res1->status != 200) {
            std::cerr << "[Agent] Error: LLM service returned an error." << std::endl;
            continue;
        }
        json llm_response1 = json::parse(llm_res1->body);
        std::string llm_content1 = llm_response1["choices"][0]["message"]["content"];
        std::cout << "[Agent] LLM chose tool: " << llm_content1 << std::endl;
        // 4. 解析LLM的回复，提取工具调用信息 Json对象
        json tool_call;
        try {
            tool_call = extract_json_from_response(llm_content1);
        } catch (const std::exception& e) {
            std::cerr << "[Agent] Error: Could not parse tool call from LLM response. " << e.what() << std::endl;
            continue;
        }

        // 5. 执行工具调用，如果大模型选择的工具名称不怎么匹配，添加这个匹配前缀
        std::string tool_name = tool_call.value("tool_name", "");
        // FIX: Add leading '/' if missing
        if (!tool_name.empty() && tool_name[0] != '/') {
            tool_name = "/" + tool_name;
        }

        std::cout << "[Agent] Executing tool '" << tool_name << "'..." << std::endl;
        json tool_params = tool_call["parameters"];
        // 5.1 判断工具类型并选择合适的请求方式调用mcp服务访问
        httplib::Result tool_res;
        if (tool_name == "/list_directory" || tool_name == "/create_directory" || tool_name == "/delete") {
            tool_res = mcp_client.Post(tool_name.c_str(), tool_params.dump(), "application/json");
        } else if (tool_name == "/list_directory_stream" || tool_name == "/help") {
            tool_res = mcp_client.Post("/list_directory", tool_params.dump(), "application/json");// 简单调用http，暂时不用流式结果
        } else {
            std::cerr << "[Agent] Error: LLM chose an unknown or unsupported tool: " << tool_name << std::endl;
            continue;
        }

        if (!tool_res || tool_res->status != 200) {
            std::cerr << "[Agent] Error: Tool execution failed. Response: " << (tool_res ? tool_res->body : "No response") << std::endl;
            continue;
        }
        
        // 6. 用工具调用结果组合提示词请求，准备让LLM生成最终回答
        std::cout << "[Agent] Asking LLM to generate final response..." << std::endl;
        std::string prompt_for_final_answer =
            "You are an agent that has just executed a tool to get information for a user.\n"
            "Based on the original request and the result from the tool, formulate a friendly, natural language response.\n\n"
            "--- USER'S ORIGINAL REQUEST ---\n"
            + user_prompt + "\n\n"
            "--- TOOL EXECUTION RESULT ---\n"
            + tool_res->body + "\n\n"
            "Now, please provide a final response to the user.";

        llm_req_body["messages"] = prompt_for_final_answer;
        auto llm_res2 = llm_client.Post("/v1/chat/completions", llm_req_body.dump(), "application/json");
        if (!llm_res2 || llm_res2->status != 200) {
            std::cerr << "[Agent] Error: LLM service returned an error on final response generation." << std::endl;
            continue;
        }
        json llm_response2 = json::parse(llm_res2->body);
        std::string final_answer = llm_response2["choices"][0]["message"]["content"];

        // 8. 打印最终回答
        std::cout << "\nAGENT: " << final_answer << std::endl;
    }

    std::cout << "[Agent] Goodbye!" << std::endl;
    return 0;
}