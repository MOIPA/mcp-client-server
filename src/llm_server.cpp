#include <iostream>
#include "LLM.h"
#include "httplib.h"
#include <nlohmann/json.hpp>
#include <ctime>
#include <string>
#include <vector>

#define CPPHTTPLIB_OPENSSL_SUPPORT
using json = nlohmann::json;

// 构造 OpenAI 风格的 JSON 响应
json build_openai_response(const std::string& input, const std::string& output, const std::string& model_name) {
    json response;
    response["id"] = "chatcmpl-" + std::to_string(std::time(nullptr));
    response["object"] = "chat.completion";
    response["created"] = std::time(nullptr);
    response["model"] = model_name;

    json choice;
    choice["index"] = 0;
    choice["finish_reason"] = "stop";

    json message;
    message["role"] = "assistant";
    message["content"] = output;
    choice["message"] = message;

    response["choices"] = {choice};

    // token 统计（示例）
    // 注意：这是一个非常粗略的估算
    int prompt_tokens = input.length() / 4;
    int completion_tokens = output.length() / 4;
    int total_tokens = prompt_tokens + completion_tokens;

    response["usage"]["prompt_tokens"] = prompt_tokens;
    response["usage"]["completion_tokens"] = completion_tokens;
    response["usage"]["total_tokens"] = total_tokens;

    return response;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <model_path> [mmproj_path] [image_path]\n", argv[0]);
        return 1;
    }

    LLM llm;

    if (!llm.load(argv[1], (argc > 2) ? argv[2] : "", 0)) {
        return 1;
    }
    // 测试模型是否正常工作
    std::cout << "Model loaded successfully." << std::endl;
    std::string user_input = "你好";
    std::string image_path = (argc > 3) ? argv[3] : "";
    std::string response = llm.send(user_input, image_path);

    std::cout << "Response: " << response << std::endl;


    // HTTP

    httplib::Server svr;

    svr.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
        if (req.get_header_value("Content-Type") != "application/json") {
            res.status = 400;
            res.set_content("Content-Type must be application/json", "text/plain");
            return;
        }

        try {
            // 解析 JSON
            auto input_json = json::parse(req.body);

            // 提取 model 和 messages
            std::string model = input_json.value("model", "my-llm");

            // 从 "messages" 字段提取prompt。
            // 注意：标准的OpenAI API格式中，"messages"应该是一个包含 "role" 和 "content" 的对象数组。
            // 这里我们简化处理，直接读取字符串。
            std::string prompt = input_json.value("messages", "");

            if (prompt.empty()) {
                res.status = 400;
                res.set_content("Invalid JSON: missing 'messages' field or it's empty.", "text/plain");
                return;
            }

            // 调用模型
            std::string output = llm.send(prompt, "");

            // 构造 OpenAI 风格响应
            auto response_json = build_openai_response(prompt, output, model);

            // 返回 JSON 响应
            res.set_content(response_json.dump(4), "application/json"); // dump(4) 用于格式化输出
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content("Invalid JSON or missing fields: " + std::string(e.what()), "text/plain");
        }
    });


    std::cout << "OpenAI-style API server running at http://localhost:8080/v1/chat/completions" << std::endl;
    svr.listen("0.0.0.0", 8080);

    // 清理资源
    llm.unload();

    return 0;
}
