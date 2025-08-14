#include <stdio.h>
#include "./encapsule.h"

/**
 * 对封装函数的调用简单测试
 */
int main(){
    // 初始化模型
    Llm llm = Llm("../model/qwen.gguf", 128, 2048);
    std::string res = llm.send("你好，世界！", true);
    printf("Response: %s\n", res.c_str());
    return 0;
}