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
    size_t start = text.find("```json");
    if (start == std::string::npos) {
        start = text.find("{");
        if (start == std::string::npos) {
             throw std::runtime_error("Could not find start of JSON in LLM response");
        }
    } else {
        start += 7; // Skip ```json
    }

    size_t end = text.rfind("}");
    if (end == std::string::npos) {
        throw std::runtime_error("Could not find end of JSON in LLM response");
    }

    std::string json_str = text.substr(start, end - start + 1);
    try {
        return json::parse(json_str);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse JSON: " + std::string(e.what()));
    }
}

int main(int argc, char* argv[]) {
    // 提示用户输入
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " \"Your natural language command\"" << std::endl;
        return 1;
    }
    std::string user_prompt = argv[1];

    // 创建HTTP客户端访问MCP和LLM的服务
    httplib::Client mcp_client(MCP_SERVICE_URL);
    httplib::Client llm_client(LLM_SERVICE_URL);
    mcp_client.set_connection_timeout(5);
    llm_client.set_connection_timeout(600); // LLM can be slow

    std::cout << "[Agent] User Prompt: " << user_prompt << std::endl;

    // 1. 读取MCP服务，获得可用的工具列表
    std::cout << "\n[Agent] Step 1: Getting available tools from MCP service..." << std::endl;
    auto mcp_res = mcp_client.Get("/help");
    if (!mcp_res || mcp_res->status != 200) {
        std::cerr << "[Agent] Error: Could not get tools from MCP service. Is it running?" << std::endl;
        return 1;
    }
    json tools_json = json::parse(mcp_res->body);
    // 1.1 打印可用工具列表
    std::cout << "[Agent] Success. Got " << tools_json["endpoints"].size() << " tools." << std::endl;
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
    std::cout << "\n[Agent] Step 2: Asking LLM to choose a tool..." << std::endl;
    json llm_req_body;
    llm_req_body["model"] = "my-llm";
    llm_req_body["messages"] = prompt_for_tool_selection;
    
    auto llm_res1 = llm_client.Post("/v1/chat/completions", llm_req_body.dump(), "application/json");
    if (!llm_res1 || llm_res1->status != 200) {
        std::cerr << "[Agent] Error: LLM service returned an error." << std::endl;
        return 1;
    }
    json llm_response1 = json::parse(llm_res1->body);
    std::string llm_content1 = llm_response1["choices"][0]["message"]["content"];
    // 3.1 打印LLM的回复
    std::cout << "[Agent] LLM suggested tool: " << llm_content1 << std::endl;

    // 4. 解析LLM的回复，提取工具调用信息 Json对象
    json tool_call;
    try {
        tool_call = extract_json_from_response(llm_content1);
    } catch (const std::exception& e) {
        std::cerr << "[Agent] Error: Could not parse tool call from LLM response. " << e.what() << std::endl;
        return 1;
    }

    // 5. 执行工具调用
    std::cout << "\n[Agent] Step 3: Executing tool '" << tool_call["tool_name"] << "' on MCP service..." << std::endl;
    std::string tool_name = tool_call["tool_name"];
    json tool_params = tool_call["parameters"];
    // 5.1 判断工具类型并选择合适的请求方式调用mcp服务访问
    httplib::Result tool_res;
    if (tool_name == "/list_directory" || tool_name == "/create_directory" || tool_name == "/delete") {
        tool_res = mcp_client.Post(tool_name.c_str(), tool_params.dump(), "application/json");
    } else if (tool_name == "/list_directory_stream") {
        tool_res = mcp_client.Post("/list_directory", tool_params.dump(), "application/json");// 简单调用http，暂时不用流式结果
    } else {
        std::cerr << "[Agent] Error: LLM chose an unknown tool: " << tool_name << std::endl;
        return 1;
    }
    // 5.2 检查工具调用结果
    if (!tool_res || tool_res->status != 200) {
        std::cerr << "[Agent] Error: Tool execution failed. Response: " << (tool_res ? tool_res->body : "No response") << std::endl;
        return 1;
    }
    std::cout << "[Agent] Success. Tool Result: " << tool_res->body << std::endl;

    // 6. 用工具调用结果组合提示词请求，准备让LLM生成最终回答
    std::cout << "\n[Agent] Step 4: Asking LLM to generate final response..." << std::endl;
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
        return 1;
    }
    json llm_response2 = json::parse(llm_res2->body);
    std::string final_answer = llm_response2["choices"][0]["message"]["content"];

    // 8. 打印最终回答
    std::cout << "\n===================================" << std::endl;
    std::cout << "[Agent] Final Answer: " << final_answer << std::endl;
    std::cout << "===================================" << std::endl;

    return 0;
}
