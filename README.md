# mcp-client-server

使用

```bash
mkdir build
cd build
cmake ..         
cmake --build .  
```

## Base Model

LLM.cpp是基于llama.cpp，封装核心模型调用逻辑

## API

基于`cpp-httplib` 和 `nlohmann/json.hpp`实现 OpenAI 风格 API

#### request

```json
{
    "model": "my-llm",
    "messages": "你好"
}
```

#### response

```json
{
    "choices": [
        {
            "finish_reason": "stop",
            "index": 0,
            "message": {
                "content": "我是你的虚拟助手，可以回答问题、提供信息和帮助你完成各种任务。",
                "role": "assistant"
            }
        }
    ],
    "created": 1755156411,
    "id": "chatcmpl-1755156411",
    "model": "my-llm",
    "object": "chat.completion",
    "usage": {
        "completion_tokens": 23,
        "prompt_tokens": 2,
        "total_tokens": 25
    }
}
```


调用：

```
curl -X POST http://localhost:8080/v1/chat/completions \
     -H "Content-Type: application/json" \
     -d '{
           "model": "my-llm",
           "messages":"你好"}'
```