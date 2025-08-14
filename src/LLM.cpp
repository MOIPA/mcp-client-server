#include <android/log.h>
#include <jni.h>
#include <iomanip>
#include <math.h>
#include <string>
#include <unistd.h>
#include "llama.h"
#include "common.h"
#include "mtmd.h"
#include "mtmd-helper.h"

#define TAG "llama-android.cpp"
#define LOGi(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGe(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

jclass la_int_var;
jmethodID la_int_var_value;
jmethodID la_int_var_inc;

std::string cached_token_chars;
std::vector<llama_chat_message> messages;
int chat_history_tokens_len = 0;    // 对话历史的所有文字长度，用于找到当前轮次的起始字符长度，储存的是文字长度，后期可以用来作为是否清理历史的阈值
int prev_tokens_len = 0;        // 目前对话历史的kv缓存长度，储存的是token的长度，后期可以用来作为是否清理历史的阈值
mtmd::context_ptr ctx_vision;   // 视觉模型上下文
mtmd::bitmaps bitmaps;          // 处理后的图片集合作为模型回答问题上下文

bool is_valid_utf8(const char * string) {
    if (!string) {
        return true;
    }

    const unsigned char * bytes = (const unsigned char *)string;
    int num;

    while (*bytes != 0x00) {
        if ((*bytes & 0x80) == 0x00) {
            // U+0000 to U+007F
            num = 1;
        } else if ((*bytes & 0xE0) == 0xC0) {
            // U+0080 to U+07FF
            num = 2;
        } else if ((*bytes & 0xF0) == 0xE0) {
            // U+0800 to U+FFFF
            num = 3;
        } else if ((*bytes & 0xF8) == 0xF0) {
            // U+10000 to U+10FFFF
            num = 4;
        } else {
            return false;
        }

        bytes += 1;
        for (int i = 1; i < num; ++i) {
            if ((*bytes & 0xC0) != 0x80) {
                return false;
            }
            bytes += 1;
        }
    }

    return true;
}

static void log_callback(ggml_log_level level, const char * fmt, void * data) {
    if (level == GGML_LOG_LEVEL_ERROR)     __android_log_print(ANDROID_LOG_ERROR, TAG, fmt, data);
    else if (level == GGML_LOG_LEVEL_INFO) __android_log_print(ANDROID_LOG_INFO, TAG, fmt, data);
    else if (level == GGML_LOG_LEVEL_WARN) __android_log_print(ANDROID_LOG_WARN, TAG, fmt, data);
    else __android_log_print(ANDROID_LOG_DEFAULT, TAG, fmt, data);
}

void init_vision_context(const char * mmprojPath,int gpu,llama_model * model,int verbosity=0) {
    mtmd_context_params mparams = mtmd_context_params_default();
    bool useGPU = true;
    if(gpu==0) useGPU = false;
    mparams.use_gpu = useGPU;  // true的情况下输出乱码，应该不支持openCL
    mparams.print_timings = true;
    int n_threads = std::max(1, std::min(8, (int) sysconf(_SC_NPROCESSORS_ONLN) - 2));
    LOGi("mmproj model Using %d threads", n_threads);
    mparams.n_threads = n_threads;
    mparams.verbosity = verbosity > 0 ? GGML_LOG_LEVEL_DEBUG : GGML_LOG_LEVEL_INFO;
    ctx_vision.reset(mtmd_init_from_file(mmprojPath, model, mparams));
    if (!ctx_vision.get()) {
        LOGe("Failed to load vision model from %s\n", mmprojPath);
    }else{
        LOGi("Loaded vision model from %s", mmprojPath);
    }
}

/**
 * 处理图片，图像模型压缩图片信息
 * @param fname
 * @return
 */
bool load_media(const char * fname) {
    if (!ctx_vision.get()) {
        LOGe("Failed to load vision model, load_media failed\n");
    }
    mtmd::bitmap bmp(mtmd_helper_bitmap_init_from_file(ctx_vision.get(), fname));
    if (!bmp.ptr) {
        return false;
    }
    bitmaps.entries.push_back(std::move(bmp));
    return true;
}

bool check_vision_ready(){
    return mtmd_support_vision(ctx_vision.get());
}

extern "C"
JNIEXPORT jlong JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_load_1model(JNIEnv *env, jobject, jstring filename, jint layers,jstring mmprojf,jint useGPU) {
    llama_model_params model_params = llama_model_default_params();
    // GPU设置
    model_params.n_gpu_layers = layers;// 尝试将前 k 层卸载到 GPU (OpenCL)

    auto path_to_model = env->GetStringUTFChars(filename, 0);
    LOGi("Loading model from %s", path_to_model);

    auto model = llama_model_load_from_file(path_to_model, model_params);
    env->ReleaseStringUTFChars(filename, path_to_model);

    if (!model) {
        LOGe("load_model() failed");
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "load_model() failed");
        return 0;
    }

    // 加载视觉模型
    auto mmprojPath = env->GetStringUTFChars(mmprojf, 0);
    init_vision_context(mmprojPath,useGPU,model,0);
    env->ReleaseStringUTFChars(mmprojf, mmprojPath);

    return reinterpret_cast<jlong>(model);
}

extern "C"
JNIEXPORT void JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_free_1model(JNIEnv *, jobject, jlong model) {
llama_model_free(reinterpret_cast<llama_model *>(model));
}

extern "C"
JNIEXPORT jlong JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_new_1context(JNIEnv *env, jobject, jlong jmodel) {
    auto model = reinterpret_cast<llama_model *>(jmodel);

    if (!model) {
        LOGe("new_context(): model cannot be null");
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Model cannot be null");
        return 0;
    }

    int n_threads = std::max(1, std::min(8, (int) sysconf(_SC_NPROCESSORS_ONLN) - 2));
    LOGi("Using %d threads", n_threads);

    llama_context_params ctx_params = llama_context_default_params();

    ctx_params.n_ctx           = 2048;
    ctx_params.n_threads       = n_threads;
    ctx_params.n_threads_batch = n_threads;

    llama_context * context = llama_new_context_with_model(model, ctx_params);

    if (!context) {
        LOGe("llama_new_context_with_model() returned null)");
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),
        "llama_new_context_with_model() returned null)");
        return 0;
    }

    return reinterpret_cast<jlong>(context);
}

extern "C"
JNIEXPORT void JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_free_1context(JNIEnv *, jobject, jlong context) {
    llama_free(reinterpret_cast<llama_context *>(context));
}

extern "C"
JNIEXPORT void JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_backend_1free(JNIEnv *, jobject) {
    llama_backend_free();
}

extern "C"
JNIEXPORT void JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_log_1to_1android(JNIEnv *, jobject) {
    llama_log_set(log_callback, NULL);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_bench_1model(
        JNIEnv *env,
        jobject,
        jlong context_pointer,
jlong model_pointer,
        jlong batch_pointer,
jint pp,
        jint tg,
jint pl,
        jint nr
    ) {
    auto pp_avg = 0.0;
    auto tg_avg = 0.0;
    auto pp_std = 0.0;
    auto tg_std = 0.0;

    const auto context = reinterpret_cast<llama_context *>(context_pointer);
    const auto model = reinterpret_cast<llama_model *>(model_pointer);
    const auto batch = reinterpret_cast<llama_batch *>(batch_pointer);

    const int n_ctx = llama_n_ctx(context);

    LOGi("n_ctx = %d", n_ctx);

    int i, j;
    int nri;
    for (nri = 0; nri < nr; nri++) {
    LOGi("Benchmark prompt processing (pp)");

    common_batch_clear(*batch);

    const int n_tokens = pp;
    for (i = 0; i < n_tokens; i++) {
    common_batch_add(*batch, 0, i, { 0 }, false);
    }

    batch->logits[batch->n_tokens - 1] = true;
    llama_memory_clear(llama_get_memory(context), false);

    const auto t_pp_start = ggml_time_us();
    if (llama_decode(context, *batch) != 0) {
    LOGi("llama_decode() failed during prompt processing");
    }
    const auto t_pp_end = ggml_time_us();

    // bench text generation

    LOGi("Benchmark text generation (tg)");

    llama_memory_clear(llama_get_memory(context), false);
    const auto t_tg_start = ggml_time_us();
    for (i = 0; i < tg; i++) {

        common_batch_clear(*batch);
        for (j = 0; j < pl; j++) {
        common_batch_add(*batch, 0, i, { j }, true);
        }

        LOGi("llama_decode() text generation: %d", i);
        const auto g1_start = ggml_time_us();
        if (llama_decode(context, *batch) != 0) {
        LOGi("llama_decode() failed during text generation");
        }
        const auto g1_end = ggml_time_us();
        LOGi("llama_decode() text generation: %d, time usage: %f ms", i, double(g1_end - g1_start)/1000.0);

    }

    const auto t_tg_end = ggml_time_us();

    llama_memory_clear(llama_get_memory(context), false);

    const auto t_pp = double(t_pp_end - t_pp_start) / 1000000.0;
    const auto t_tg = double(t_tg_end - t_tg_start) / 1000000.0;

    const auto speed_pp = double(pp) / t_pp;
    const auto speed_tg = double(pl * tg) / t_tg;

    pp_avg += speed_pp;
    tg_avg += speed_tg;

    pp_std += speed_pp * speed_pp;
    tg_std += speed_tg * speed_tg;

    LOGi("pp %f t/s, tg %f t/s", speed_pp, speed_tg);
    }

    pp_avg /= double(nr);
    tg_avg /= double(nr);

    if (nr > 1) {
    pp_std = sqrt(pp_std / double(nr - 1) - pp_avg * pp_avg * double(nr) / double(nr - 1));
    tg_std = sqrt(tg_std / double(nr - 1) - tg_avg * tg_avg * double(nr) / double(nr - 1));
    } else {
    pp_std = 0;
    tg_std = 0;
    }

    char model_desc[128];
    llama_model_desc(model, model_desc, sizeof(model_desc));

    const auto model_size     = double(llama_model_size(model)) / 1024.0 / 1024.0 / 1024.0;
    const auto model_n_params = double(llama_model_n_params(model)) / 1e9;

    const auto backend    = "(Android)"; // TODO: What should this be?

    std::stringstream result;
    result << std::setprecision(2);
    result << "| model | size | params | backend | test | t/s |\n";
    result << "| --- | --- | --- | --- | --- | --- |\n";
    result << "| " << model_desc << " | " << model_size << "GiB | " << model_n_params << "B | " << backend << " | pp " << pp << " | " << pp_avg << " ± " << pp_std << " |\n";
    result << "| " << model_desc << " | " << model_size << "GiB | " << model_n_params << "B | " << backend << " | tg " << tg << " | " << tg_avg << " ± " << tg_std << " |\n";

    return env->NewStringUTF(result.str().c_str());
}

extern "C"
JNIEXPORT jlong JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_new_1batch(JNIEnv *, jobject, jint n_tokens, jint embd, jint n_seq_max) {
    llama_batch *batch = new llama_batch {
            0,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
    };

    if (embd) {
    batch->embd = (float *) malloc(sizeof(float) * n_tokens * embd);
    } else {
    batch->token = (llama_token *) malloc(sizeof(llama_token) * n_tokens);
    }

    batch->pos      = (llama_pos *)     malloc(sizeof(llama_pos)      * n_tokens);
    batch->n_seq_id = (int32_t *)       malloc(sizeof(int32_t)        * n_tokens);
    batch->seq_id   = (llama_seq_id **) malloc(sizeof(llama_seq_id *) * n_tokens);
    for (int i = 0; i < n_tokens; ++i) {
    batch->seq_id[i] = (llama_seq_id *) malloc(sizeof(llama_seq_id) * n_seq_max);
    }
    batch->logits   = (int8_t *)        malloc(sizeof(int8_t)         * n_tokens);

    return reinterpret_cast<jlong>(batch);
}

extern "C"
JNIEXPORT void JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_free_1batch(JNIEnv *, jobject, jlong batch_pointer) {
    //llama_batch_free(*reinterpret_cast<llama_batch *>(batch_pointer));
    const auto batch = reinterpret_cast<llama_batch *>(batch_pointer);
    delete batch;
}

extern "C"
JNIEXPORT jlong JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_new_1sampler(JNIEnv *, jobject) {
    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = true;
    llama_sampler * smpl = llama_sampler_chain_init(sparams);
//    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.6f));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(20));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.95,1));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    return reinterpret_cast<jlong>(smpl);
}

extern "C"
JNIEXPORT void JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_free_1sampler(JNIEnv *, jobject, jlong sampler_pointer) {
    llama_sampler_free(reinterpret_cast<llama_sampler *>(sampler_pointer));
}

extern "C"
JNIEXPORT void JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_backend_1init(JNIEnv *, jobject) {
    llama_backend_init();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_system_1info(JNIEnv *env, jobject) {
    return env->NewStringUTF(llama_print_system_info());
}


extern "C"
JNIEXPORT jint JNICALL
/**
 * 视觉模型处理提示词
 * @param env
 * @param context_pointer
 * @param batch_pointer
 * @param jtext
 * @param format_chat
 * @param n_len
 * @param picf
 * @return
 */
Java_cn_com_zte_app_demollm_LLMAndroid_completion_1init_1vision(
        JNIEnv *env,
        jobject,
        jlong context_pointer,
        jlong batch_pointer,
        jstring jtext,
        jboolean format_chat,
        jint n_len,
        jstring picf
) {

    cached_token_chars.clear();

    const auto text = env->GetStringUTFChars(jtext, 0);
    const auto context = reinterpret_cast<llama_context *>(context_pointer);
    const auto batch = reinterpret_cast<llama_batch *>(batch_pointer);
    auto picPath = env->GetStringUTFChars(picf, 0);
    // chat历史记忆，会话预处理
    std::vector<char> formatted(llama_n_ctx(context));
    const auto model = llama_get_model(context);
    const llama_vocab * vocab = llama_model_get_vocab(model);
    const char * tmpl = llama_model_chat_template(model, /* name */ nullptr);
    std::string user_text(text);
    if(picPath!="" && load_media(picPath)){
        LOGi("pic %s loaded successfully",picPath);
        user_text = mtmd_default_marker() + user_text;
    }
    messages.push_back({"user", strdup(user_text.c_str())}); // 添加用户会话
    int new_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    if (new_len > (int)formatted.size()) {
        formatted.resize(new_len);
        new_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    }
    if (new_len < 0) {
        LOGe("failed to apply the chat template\n");
    }
    std::string prompt(formatted.begin() + chat_history_tokens_len, formatted.begin() + new_len); // 新的prompt
    LOGi("Current Chat History (Size: %zu):", messages.size());
    for (size_t i = 0; i < messages.size(); ++i) {
        const llama_chat_message& msg_to_log = messages[i];
        const char* role_to_log = msg_to_log.role ? msg_to_log.role : "[null role]";
        const char* content_to_log = msg_to_log.content ? msg_to_log.content : "[null content]";
        LOGi("  Msg %zu: Role: \"%s\", Content: \"%s\"", i, role_to_log, content_to_log);
    }

    env->ReleaseStringUTFChars(picf, picPath);
    LOGi("prompt:%s",prompt.c_str());
    // 正常处理
    bool parse_special = (format_chat == JNI_TRUE);
    // 视觉内容处理
    bool add_bos = true; // 是否是第一句话 对于一个全新对话的第一段用户输入，或者当 KV 缓存被清除后，通常需要添加 BOS token。所以 add_bos 设为 true 是合适的
    if(prev_tokens_len > 0){
        add_bos = false;
    }
    LOGi("cpp:prev_tokens_len = %d, add_bos = %d",prev_tokens_len,add_bos);

    mtmd_input_text mtmd_text;
    mtmd_text.text          = prompt.c_str();
    mtmd_text.add_special   = add_bos;
    mtmd_text.parse_special = true;
    int n_batch = 512; // 一个输入会被拆分成 n_batch 个 token
    // 初始化视觉输入 结合图像和文本内容
    mtmd::input_chunks chunks(mtmd_input_chunks_init());
    auto bitmaps_c_ptr = bitmaps.c_ptr();
    int32_t res = mtmd_tokenize(ctx_vision.get(),
                                chunks.ptr.get(), // output
                                &mtmd_text, // text
                                bitmaps_c_ptr.data(),
                                bitmaps_c_ptr.size());
    if (res != 0) {
        LOGe("mtmd Unable to tokenize prompt, res = %d\n", res);
    }
    LOGi("cpp: mtmd tokenized prompt, res = %d\n", res);

    bitmaps.entries.clear(); // 清理图片
    llama_pos new_n_past;
    LOGi("cpp: cleared bitmaps");
    // 提示词处理过程，图像计算完后的向量和文本向量融合
    if (mtmd_helper_eval_chunks(ctx_vision.get(),   //
                                context, // lctx
                                chunks.ptr.get(), // chunks
                                prev_tokens_len, // n_past
                                0, // seq_id
                                n_batch, // n_batch
                                true, // logits_last  设置了logits_last会自动计算下一个字符logits，loop开始只需要采样
                                &new_n_past)) {
        LOGe("Unable to eval prompt\n");
    }
    LOGi("cpp: evaluated prompt");
    // 本轮提示词长度，用于返回给loop，决定是否上下文过长超出设置的kv缓冲大小，停止输出用的（prompt和输出都在一个kv缓存）
    int prompt_token_len = new_n_past - prev_tokens_len;
    prev_tokens_len = new_n_past;
    env->ReleaseStringUTFChars(jtext, text);
    LOGi("cpp: prompt_token_len = %d", prompt_token_len);
    return prompt_token_len;
}

extern "C"
JNIEXPORT jint JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_completion_1init(
        JNIEnv *env,
        jobject,
        jlong context_pointer,
        jlong batch_pointer,
        jstring jtext,
        jboolean format_chat,
        jint n_len
    ) {

    cached_token_chars.clear();

    const auto text = env->GetStringUTFChars(jtext, 0);
    const auto context = reinterpret_cast<llama_context *>(context_pointer);
    const auto batch = reinterpret_cast<llama_batch *>(batch_pointer);
    // chat历史记忆，会话预处理
    std::vector<char> formatted(llama_n_ctx(context));
    const auto model = llama_get_model(context);
    const llama_vocab * vocab = llama_model_get_vocab(model);
    const char * tmpl = llama_model_chat_template(model, /* name */ nullptr);
    messages.push_back({"user", strdup(text)}); // 添加用户会话
    int new_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    if (new_len > (int)formatted.size()) {
        formatted.resize(new_len);
        new_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    }
    if (new_len < 0) {
        LOGe("failed to apply the chat template\n");
    }
    std::string prompt(formatted.begin() + chat_history_tokens_len, formatted.begin() + new_len); // 新的prompt
    LOGi("Current Chat History (Size: %zu):", messages.size());
    for (size_t i = 0; i < messages.size(); ++i) {
        const llama_chat_message& msg_to_log = messages[i];
        const char* role_to_log = msg_to_log.role ? msg_to_log.role : "[null role]";
        const char* content_to_log = msg_to_log.content ? msg_to_log.content : "[null content]";
        LOGi("  Msg %zu: Role: \"%s\", Content: \"%s\"", i, role_to_log, content_to_log);
    }
    LOGi("prompt:%s",prompt.c_str());

    // 正常处理
    bool parse_special = (format_chat == JNI_TRUE);
    const auto tokens_list = common_tokenize(context, prompt, true, parse_special);

    auto n_ctx = llama_n_ctx(context);
    int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(context), 0);
    auto n_kv_req = tokens_list.size() + n_len + n_ctx_used;  // 本轮次会用到的最大kv缓存大小

    LOGi("n_len = %d, n_ctx = %d, n_kv_req = %d", n_len, n_ctx, n_kv_req);

    if (n_kv_req > n_ctx) {
        LOGe("error: n_kv_req > n_ctx, the required KV cache size is not big enough, clear history kv cache");
        llama_memory_clear(llama_get_memory(context), true);
        prev_tokens_len = 0;
    }

    for (auto id : tokens_list) {
    LOGi("token: `%s`-> %d ", common_token_to_piece(context, id).c_str(), id);
    }
    common_batch_clear(*batch);
    int pos = prev_tokens_len;
    LOGi("pos = %d", pos);
    // evaluate the initial prompt
    for (auto i = 0; i < tokens_list.size(); i++) {
    common_batch_add(*batch, tokens_list[i], i+pos, { 0 }, false);
    }

    // llama_decode will output logits only for the last token of the prompt
    batch->logits[batch->n_tokens - 1] = true;

    if (llama_decode(context, *batch) != 0) {
    LOGe("llama_decode() failed");
    }

    env->ReleaseStringUTFChars(jtext, text);
    prev_tokens_len += batch->n_tokens;
    return batch->n_tokens;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_completion_1loop(
        JNIEnv * env,
        jobject,
        jlong context_pointer,
        jlong batch_pointer,
        jlong sampler_pointer,
        jint n_len
        ) {
    const auto context = reinterpret_cast<llama_context *>(context_pointer);
    const auto batch   = reinterpret_cast<llama_batch   *>(batch_pointer);
    const auto sampler = reinterpret_cast<llama_sampler *>(sampler_pointer);
    const auto model = llama_get_model(context);
    const auto vocab = llama_model_get_vocab(model);

//    if (!la_int_var) la_int_var = env->GetObjectClass(intvar_ncur);
//    if (!la_int_var_value) la_int_var_value = env->GetMethodID(la_int_var, "getValue", "()I");
//    if (!la_int_var_inc) la_int_var_inc = env->GetMethodID(la_int_var, "inc", "()V");

    // sample the most likely token
    const auto new_token_id = llama_sampler_sample(sampler, context, -1);

//    const auto n_cur = env->CallIntMethod(intvar_ncur, la_int_var_value);
    if (llama_vocab_is_eog(vocab, new_token_id) ) {
        LOGi("DONE Loop retrun nullptr, n_len:%d" ,n_len); // 本轮生成结果过长了，停止
        return nullptr;
    }

    auto new_token_chars = common_token_to_piece(context, new_token_id);
    cached_token_chars += new_token_chars;

    jstring new_token = nullptr;
    if (is_valid_utf8(cached_token_chars.c_str())) {
        new_token = env->NewStringUTF(cached_token_chars.c_str());
        LOGi("cached: %s, new_token_chars: `%s`, id: %d", cached_token_chars.c_str(), new_token_chars.c_str(), new_token_id);
        cached_token_chars.clear();
    } else {
        new_token = env->NewStringUTF("");
    }

    common_batch_clear(*batch);
    common_batch_add(*batch, new_token_id, prev_tokens_len, {0 }, true);

//    env->CallVoidMethod(intvar_ncur, la_int_var_inc);
    prev_tokens_len++;

    if (llama_decode(context, *batch) != 0) {
    LOGe("llama_decode() returned null");
    }

    return new_token;
}

extern "C"
JNIEXPORT void JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_kv_1cache_1clear(JNIEnv *, jobject, jlong context) {
    llama_memory_clear(llama_get_memory(reinterpret_cast<llama_context *>(context)), true);
    prev_tokens_len=0;
    chat_history_tokens_len = 0;
    messages.clear();
}

extern "C"
JNIEXPORT void JNICALL
Java_cn_com_zte_app_demollm_LLMAndroid_supply(JNIEnv *env, jobject, jstring jtext,jlong jmodel) {
    const auto response = env->GetStringUTFChars(jtext, 0);
    messages.push_back({"assistant", strdup(response)});
    auto model = reinterpret_cast<llama_model *>(jmodel);
    const char * tmpl = llama_model_chat_template(model, /* name */ nullptr);
    chat_history_tokens_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), false, nullptr, 0);
    if (chat_history_tokens_len < 0) {
        LOGe("failed to apply the chat template\n");
    }
    env->ReleaseStringUTFChars(jtext, response);
}