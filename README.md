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

调用示例: ` curl -X POST http://localhost:8080/v1/chat/completions -H "Content-Type: application/json" -d '{"model": "my-llm","messages":"你好"}' `

```json
{
    "choices": [
        {
            "finish_reason": "stop",
            "index": 0,
            "message": {
                "content": "<think>\n嗯，用户发来的是“你好”，看起来是个简单的问候。首先，我需要确定用户是否需要帮助，或者只是想打招呼。根据之前的对话历史，用户可能是在测试我的反应，或者想开始一个对话。\n\n接下来，我要考虑如何回应才能既友好又专业。应该保持简洁，避免冗长，同时提供帮助。比如，可以回复“你好！有什么可以帮您的吗？”这样既亲切又开放，让用户知道他们可以随时提问。\n\n还要注意用户可能的意图，他们可能希望得到具体的帮助，或者只是想交流。所以回应需要足够灵活，让用户觉得被重视，同时又不会显得太冗长。另外，保持语气友好，使用表情符号可能增加亲和力，但根据之前的设定，可能不需要使用表情符号，所以保持文字简洁。\n\n最后，确保回应符合规范，不包含任何可能引起问题的信息，同时保持自然流畅。总结下来，一个合适的回应应该是简短、友好，并且开放式的，让用户有继续对话的意愿。\n</think>\n\n你好！有什么可以帮您的吗？",
                "role": "assistant"
            }
        }
    ],
    "created": 1755237389,
    "id": "chatcmpl-1755237389",
    "model": "my-llm",
    "object": "chat.completion",
    "usage": {
        "completion_tokens": 288,
        "prompt_tokens": 1,
        "total_tokens": 289
    }
}
```

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

无状态服务，不支持多用户session_id，只是个简单示例

启动支持`HTTP/SSE`的服务器：`./mcp_server`（编译后输出的可执行文件）

用于测试的mcp服务，操控本地目录，可以查看本地目录，删除本地目录，创建本地目录

基于 `HTTP/SSE`实现，其中只有查看目录`list_directory_stream`是`SSE`。 (SSE:流式传输，不是等待返回全部token，而是一个token一个token的返回)

#### 获取服务描述列表

`curl http://localhost:8081/help`

```json

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
}
```

#### 单个服务调用

测试:
```bash
curl -X POST http://localhost:8081/list_directory \
   -H "Content-Type: application/json" \
   -d '{"path": "."}'
```

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

## 4. agent

所依赖的服务启动后，即可启动agent服务: `./agent`

依赖：
+ 支持openAi接口格式的llm服务：`./llm_server`
+ 支持mcp格式以及传输协议的工具服务：`./mcp_server`

#### 记忆问题

暂未支持

模型自己的kv缓存只能算是短期记忆，真正希望agent记住历史会话需要增加详细会话记录

#### 模型和工具兼容性问题

模型根据工具描述输出的json格式健壮性问题，需要更好的extract，如果模型有think或者其他输出时，抽取llm回答内的json就可能会出错，因此抽取函数需要针对详细模型做更加匹配的适配或者构建更强大的抽取功能

#### 使用工具抉择

用户不一定每次都会调用某个工具，需要在进入模型问答层之前先用一个提示词做用户意图判断，看是否是直接回答用户问题还是调用工具回答

#### 使用示例:

```
$ ./agent                                                                                      INT ✘  4m 53s   base   11:28:17  

=========================================================
            MCP Agent (Conversational)                 
=========================================================
Enter your command, or type 'exit' or 'quit' to leave.

YOU: 当前目录下有什么
[Agent] Getting available tools...
[Agent] Asking LLM to choose a tool...
[Agent] LLM chose tool: <think>
Okay, the user asked, "当前目录下有什么" which means "What's in the current directory?" So I need to use the tool that lists directory contents. The available tools include "/list_directory" which takes a path parameter. Since the user mentioned "当前目录", the path should be the current directory, which is "." by default. So the tool_name is "list_directory" and parameters {"path": "."}.
</think>

{
  "tool_name": "list_directory",
  "parameters": {
    "path": "."
  }
}
[Agent] Executing tool '/list_directory'...
[Agent] Asking LLM to generate final response...

AGENT: <think>
Okay, let's see. The user asked, "当前目录下有什么" which means "What's in the current directory?" The tool provided a response with the contents array.

So, the user wants to know the files and folders in their current directory. The tool's result shows that the directory has several files and folders. I need to list them in a friendly way. Let me check the contents again: CMakeFiles, mcp_server, agent, CMakeCache.txt, llm, Makefile, llm_server, cmake_install.cmake. 

I should present these in a clear, natural language format. Maybe start by stating that the current directory contains these items, then list them each with a brief description. Make sure to mention that they can ask for more info if needed. Keep it simple and conversational.
</think>

当前目录下包含以下内容：  
- `CMakeFiles`  
- `mcp_server`  
- `agent`  
- `CMakeCache.txt`  
- `llm`  
- `Makefile`  
- `llm_server`  
- `cmake_install.cmake`  

如果需要更详细的说明，欢迎随时问我！

YOU: 

```