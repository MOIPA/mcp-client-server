#include "../include/llama.h"  // llama.cpp 的主头文件
#include <cstdio>             // C标准输入输出（printf等）
#include <cstring>            // C字符串操作（strcpy等）
#include <string>             // C++字符串
#include <vector>             // C++动态数组

int main(int argc, char ** argv) {
    // 模型路径（gguf格式模型）
    std::string model_path = "../model/qwen.gguf";

    // 输入的提示词（prompt）
    std::string prompt = "what is llama";

    // GPU 加速的层数（设置为99表示尽可能多的层都放到GPU）
    int ngl = 99;

    // 要生成的 token 数量
    int n_predict = 320;


    // 加载所有支持的后端（CPU、CUDA、Vulkan、Metal 等）
    ggml_backend_load_all();


    // ================== 第一步：加载模型 ==================

    // 初始化模型参数（默认参数）
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = ngl;  // 设置 GPU 加速层数

    // 从文件加载模型
    llama_model * model = llama_model_load_from_file(model_path.c_str(), model_params);

    // 检查模型是否加载成功
    if (model == NULL) {
        fprintf(stderr , "%s: error: 无法加载模型\n" , __func__);
        return 1;
    }

    // 获取模型的词汇表（用于 token 和字符串之间的转换）
    const llama_vocab * vocab = llama_model_get_vocab(model);


    // ================== 第二步：Tokenize 提示词 ==================

    // 先计算 prompt 会被 tokenize 成多少个 token
    // llama_tokenize 返回负数表示需要的 buffer 大小
    const int n_prompt = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), NULL, 0, true, true);

    // 分配空间并真正 tokenize 提示词
    std::vector<llama_token> prompt_tokens(n_prompt);
    if (llama_tokenize(vocab, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(), true, true) < 0) {
        fprintf(stderr, "%s: error: tokenize 提示词失败\n", __func__);
        return 1;
    }

    // 打印原始 prompt 的每个 token（调试用）
    for (auto id : prompt_tokens) {
        char buf[128]; 
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            fprintf(stderr, "%s: error: failed to convert token to piece\n", __func__);
            return 1;
        }
        std::string s(buf, n);
        printf("%s", s.c_str());
    }
    printf("\n");


    // ================== 第三步：初始化上下文 ==================

    // 初始化上下文参数
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = n_prompt + n_predict - 1;  // 设置上下文最大长度
    ctx_params.n_batch = n_prompt;                // 一个 batch 最多处理多少 token
    ctx_params.no_perf = false;                   // 开启性能统计

    // 使用模型初始化上下文
    llama_context * ctx = llama_init_from_model(model, ctx_params);

    // 检查上下文是否创建成功
    if (ctx == NULL) {
        fprintf(stderr , "%s: 创建llama_context失败 \n" , __func__);
        return 1;
    }


    // ================== 第四步：初始化采样器 ==================

    // 初始化采样器链（支持多个采样策略）
    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = false;

    // 创建采样器链
    llama_sampler * smpl = llama_sampler_chain_init(sparams);

    // 添加一个贪婪采样器（总是选概率最大的 token）
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());


    // ================== 第五步：主推理循环 ==================

    // 记录开始时间，用于计算生成速度
    const auto t_main_start = ggml_time_us();

    // 实际生成的 token 数量
    int n_decode = 0;

    // 新生成的 token ID
    llama_token new_token_id;
    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());

    // 主循环，直到生成完 n_predict 个 token 或遇到结束符
    for (int n_pos = 0; n_pos + batch.n_tokens < n_prompt + n_predict; ) {

        // 将这个 batch 输入模型进行推理（前向计算）
        if (llama_decode(ctx, batch)) {
            fprintf(stderr, "%s : failed to eval, return code %d\n", __func__, 1);
            return 1;
        }

        // 当前 batch 的 token 数量加到已处理位置
        n_pos += batch.n_tokens;


        // ================== 第六步：采样下一个 token ==================

        // 使用采样器从模型输出中采样下一个 token
        new_token_id = llama_sampler_sample(smpl, ctx, -1);

        // 检查是否生成结束（遇到 <EOS> 等结束符）
        if (llama_vocab_is_eog(vocab, new_token_id)) {
            break;
        }

        // 将新生成的 token 转换为字符串输出
        char buf [128];
        int n = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            fprintf(stderr, "%s: error: failed to convert token to piece\n", __func__);
            return 1;
        }
        std::string s(buf, n);
        printf("%s", s.c_str());
        fflush(stdout);  // 强制刷新输出缓冲区（让输出立刻显示）

        // 将新生成的 token 放入下一轮的 batch
        batch = llama_batch_get_one(&new_token_id, 1);
        n_decode += 1;
    }

    printf("\n");


    // ================== 第七步：性能统计 ==================

    const auto t_main_end = ggml_time_us();

    // 输出生成速度（token/s）
    fprintf(stderr, "%s: decoded %d tokens in %.2f s, speed: %.2f t/s\n",
            __func__, n_decode, (t_main_end - t_main_start) / 1000000.0f, n_decode / ((t_main_end - t_main_start) / 1000000.0f));

    // 输出采样器和上下文的性能统计信息
    fprintf(stderr, "\n");
    llama_perf_sampler_print(smpl);
    llama_perf_context_print(ctx);
    fprintf(stderr, "\n");


    // ================== 第八步：释放资源 ==================

    llama_sampler_free(smpl);       // 释放采样器
    llama_free(ctx);                // 释放上下文
    llama_model_free(model);        // 释放模型

    return 0;
}