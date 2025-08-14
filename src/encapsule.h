#ifndef ENCAPSULE_H
#define ENCAPSULE_H
#include <iostream>
#include <string>
#include <vector>
#include "../include/llama.h"
#include "../include/common.h"

bool is_valid_utf8(const char * string);
llama_model * loadModel(std::string fileName);

void freeModel(llama_model* model);

llama_context * newContext(llama_model* model,int context_size=2048);

void freeContext(llama_context* context);

void loadBackendInit();

/**
 * 本函数创建空的batch，用于装载数据
 * @param n_tokens  这个batch最多有多少个token
 * @param embd      输入是否是embedding，如果是传入embedding的长度，一般都是0 不采用embd
 * @param n_seq_max 最大序列，同时处理多个用户的问题时，一般都是1
 */
llama_batch* newBatch(int n_tokens,int embd=0,int n_seq_max=1);

void freeBatch(llama_batch * batch);

llama_sampler* newSampler();

void freeSampler(llama_sampler * sampler);

/**
 * benchmark 模型
 * @param pp    prompt process，处理提示词的长度，主要是计算kv缓存和最后一个字符的下一字符logits
 * @param tg    text generation 生成内容长度，每个字符都要计算kv缓存+logits
 * @param pl    paral line  并行生成数量，同时处理三个用户的内容就是3，一般是1
 * @param nr    number run  测试的轮数，一般是1或者3，轮数越多测速结果越平均
 */
std::string benchModel(llama_context * context,llama_model* model,
    llama_batch * batch,int pp,int tg,int pl,int nr);

/**
 * 初始化对话，主要工作是处理提示词，生成kv缓存计算最后一个字符对应的下一个字符的logits
 * @param formatChat    是否处理提示词里的特殊字符，如果false，<EOS>这种特殊字符普通处理了
 * @param nLen          生成的长度
 * @param text          输入的提示词
 * @return 返回提示词最后一词的位置，用于后面loop添加batch的时候提供初始位置
 */
int completionInit(llama_context * context,llama_batch* batch, std::string text,bool formatChat,int n_len);

/**
 * 每次生成一个token
 * @param n_len 生成的长度
 * @param n_cur 当前已经生成的长度
 * @return 返回生成的内容，直到遇到结束符或者达到n_len长度
 */
std::string completionLoop(llama_context * context,llama_batch* batch,
llama_sampler* sampler,int n_len,int* n_cur);

void kvCacheClear(llama_context * context);

class Llm{
public:
    llama_model * model = nullptr;
    llama_context * context = nullptr;
    llama_batch * batch = nullptr;
    llama_sampler * sampler = nullptr;
    int n_len = 0; // 生成的长度
    int n_cur = 0; // 当前已经生成的长度

    Llm(std::string fileName,int n_len=128,int context_size=2048);

    ~Llm();

    // bench测评
    std::string bench(int pp,int tg,int pl,int nr);
    // 对话
    std::string send(std::string message,bool formatChat=true);
};
#endif  