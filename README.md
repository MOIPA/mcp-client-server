# mcp-client-server

基于`llama.cpp`,`cpp-httplib`,`nlohmann/json.hpp`

from scratch的方式实现mcp-server，llm，client 从头实现mcp协议的整个流程

使用

```bash
mkdir build
cd build
cmake ..         
cmake --build . 
```

## 1. Base Model

LLM.cpp是基于llama.cpp，封装核心模型调用逻辑，编译完成后build目录下`llm_server`执行`./llm_server`启动大模型服务

## 2. LLM API

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


#### 调用示例

```
curl -X POST http://localhost:8080/v1/chat/completions \
     -H "Content-Type: application/json" \
     -d '{
           "model": "my-llm",
           "messages":"你好"}'
```

## 3. mcp-server

启动支持`HTTP/SSE`的服务器：`./mcp_server`（编译后输出的可执行文件）

用于测试的mcp服务，操控本地目录，可以查看本地目录，删除本地目录，创建本地目录

基于 `HTTP/SSE`实现，其中只有查看目录`list_directory_stream`是`SSE`。 (SSE:流式传输，不是等待返回全部token，而是一个token一个token的返回)

#### 获取服务描述列表

```json
curl http://localhost:8081/help

{
    "endpoints": [
        {
            "description": "Lists the contents of a directory. Returns the contents in a single JSON response.",
            "method": "POST",
            "path": "/list_directory",
            "request_body": {
                "schema": {
                    "path": "string (absolute or relative path)"
                },
                "type": "application/json"
            }
        },
        {
            "description": "Creates a new directory at the specified path.",
            "method": "POST",
            "path": "/create_directory",
            "request_body": {
                "schema": {
                    "path": "string (path for the new directory)"
                },
                "type": "application/json"
            }
        },
        {
            "description": "Deletes a file or a directory (recursively). Restricted to subdirectories of the server's working directory.",
            "method": "POST",
            "path": "/delete",
            "request_body": {
                "schema": {
                    "path": "string (path to the item to delete)"
                },
                "type": "application/json"
            }
        },
        {
            "description": "Lists the contents of a directory using a Server-Sent Events (SSE) stream. Each entry is sent as a separate event.",
            "method": "GET",
            "path": "/list_directory_stream",
            "query_parameters": {
                "path": "string (absolute or relative path, defaults to '.' )"
            }
        },
        {
            "description": "Returns this API description.",
            "method": "GET",
            "path": "/help"
        }
    ],
    "service": "MCP File System Service",
    "version": "1.1.0"
}%        
```

#### 单个服务调用

测试: `curl -N http://localhost:8081/list_directory_stream?path=.` （-N 或 --no-buffer 禁用缓冲区，流式场景使用，数据收到即发出）

返回结果：

```
$ curl -N http://localhost:8081/list_directory_stream\?path\=.

id: 1
event: file_entry
data: {"filename":"CMakeFiles","is_directory":true}

id: 2
event: file_entry
data: {"filename":"mcp_server","is_directory":false}

id: 3
event: file_entry
data: {"filename":"CMakeCache.txt","is_directory":false}

id: 4
event: file_entry
data: {"filename":"llm","is_directory":false}

id: 5
event: file_entry
data: {"filename":"Makefile","is_directory":false}

id: 6
event: file_entry
data: {"filename":"cmake_install.cmake","is_directory":false}

event: end_of_stream
data: {}

curl: (18) transfer closed with outstanding read data remaining                                           
```