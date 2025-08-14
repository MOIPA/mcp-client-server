#include "LLM.h"
#include <iostream>
#include <iomanip>
#include <math.h>
#include <unistd.h>
#include <cstring>
#include "common.h"
#include "mtmd-helper.h"

#define TAG "llama-linux.cpp"
#define LOGi(...) printf(__VA_ARGS__); printf("\n")
#define LOGe(...) printf(__VA_ARGS__); printf("\n")

LLM::LLM() : model(nullptr), context(nullptr), batch(nullptr), sampler(nullptr), prev_tokens_len(0), chat_history_tokens_len(0) {}

LLM::~LLM() {
    unload();
}

bool LLM::load(const std::string& model_path, const std::string& mmproj_path, int gpu_layers) {
    backend_init();
    log_to_console();

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = gpu_layers;

    model = llama_model_load_from_file(model_path.c_str(), model_params);
    if (!model) {
        LOGe("load_model() failed");
        return false;
    }

    if (!mmproj_path.empty()) {
        init_vision_context(mmproj_path.c_str(), gpu_layers, model, 0);
    }

    context = LLM::new_context(model);
    if (!context) {
        LLM::free_model(model);
        return false;
    }

    batch = LLM::new_batch(512, 0, 1);
    sampler = LLM::new_sampler();

    return true;
}

void LLM::unload() {
    if (sampler) {
        LLM::free_sampler(sampler);
        sampler = nullptr;
    }
    if (batch) {
        LLM::free_batch(batch);
        batch = nullptr;
    }
    if (context) {
        LLM::free_context(context);
        context = nullptr;
    }
    if (model) {
        LLM::free_model(model);
        model = nullptr;
    }
    backend_free();
}

std::string LLM::send(const std::string& user_input, const std::string& image_path) {
    int n_len = 1280;
    fprintf(stdout, "sending to model...\n");

    // if (check_vision_ready()) {
    //     fprintf(stdout, "Vision model is ready\n");
    //     completion_init_vision(user_input.c_str(), true, n_len, image_path.c_str());
    // } else {
    //     fprintf(stdout, "Vision model is not ready\n");
    //     completion_init(user_input.c_str(), true, n_len);
    // }
    completion_init(user_input.c_str(), true, n_len);

    std::string result = "";
    for (int i = 0; i < n_len; i++) {
        std::string token = completion_loop(n_len);
        if (token.empty()) {
            break;
        }
        result += token;
    }

    supply(result.c_str());

    return result;
}


void LLM::init_vision_context(const char * mmprojPath,int gpu,llama_model * model,int verbosity) {
    mtmd_context_params mparams = mtmd_context_params_default();
    bool useGPU = true;
    if(gpu==0) useGPU = false;
    mparams.use_gpu = useGPU;
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

bool LLM::load_media(const char * fname) {
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

bool LLM::check_vision_ready(){
    return mtmd_support_vision(ctx_vision.get());
}

int LLM::completion_init_vision(const char* text, bool format_chat, int n_len, const char* picf) {
    cached_token_chars.clear();

    std::vector<char> formatted(llama_n_ctx(context));
    const auto model = llama_get_model(context);
    const llama_vocab * vocab = llama_model_get_vocab(model);
    const char * tmpl = llama_model_chat_template(model, /* name */ nullptr);
    std::string user_text(text);
    if(picf !="" && load_media(picf)){
        LOGi("pic %s loaded successfully",picf);
        user_text = mtmd_default_marker() + user_text;
    }
    messages.push_back({"user", strcpy(new char[user_text.length() + 1], user_text.c_str())});
    int new_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    if (new_len > (int)formatted.size()) {
        formatted.resize(new_len);
        new_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    }
    if (new_len < 0) {
        LOGe("failed to apply the chat template\n");
    }
    std::string prompt(formatted.begin() + chat_history_tokens_len, formatted.begin() + new_len);
    LOGi("Current Chat History (Size: %zu):", messages.size());
    for (size_t i = 0; i < messages.size(); ++i) {
        const llama_chat_message& msg_to_log = messages[i];
        const char* role_to_log = msg_to_log.role ? msg_to_log.role : "[null role]";
        const char* content_to_log = msg_to_log.content ? msg_to_log.content : "[null content]";
        LOGi("  Msg %zu: Role: \"%s\", Content: \"%s\"", i, role_to_log, content_to_log);
    }

    LOGi("prompt:%s",prompt.c_str());

    bool parse_special = format_chat;
    bool add_bos = true;
    if(prev_tokens_len > 0){
        add_bos = false;
    }
    LOGi("cpp:prev_tokens_len = %d, add_bos = %d",prev_tokens_len,add_bos);

    mtmd_input_text mtmd_text;
    mtmd_text.text          = prompt.c_str();
    mtmd_text.add_special   = add_bos;
    mtmd_text.parse_special = true;
    int n_batch = 512;
    mtmd::input_chunks chunks(mtmd_input_chunks_init());
    auto bitmaps_c_ptr = bitmaps.c_ptr();
    int32_t res = mtmd_tokenize(ctx_vision.get(),
                                chunks.ptr.get(),
                                &mtmd_text,
                                bitmaps_c_ptr.data(),
                                bitmaps_c_ptr.size());
    if (res != 0) {
        LOGe("mtmd Unable to tokenize prompt, res = %d\n", res);
    }
    LOGi("cpp: mtmd tokenized prompt, res = %d\n", res);

    bitmaps.entries.clear();
    llama_pos new_n_past;
    LOGi("cpp: cleared bitmaps");

    if (mtmd_helper_eval_chunks(ctx_vision.get(),
                                context,
                                chunks.ptr.get(),
                                prev_tokens_len,
                                0,
                                n_batch,
                                true,
                                &new_n_past)) {
        LOGe("Unable to eval prompt\n");
    }
    LOGi("cpp: evaluated prompt");

    int prompt_token_len = new_n_past - prev_tokens_len;
    prev_tokens_len = new_n_past;
    LOGi("cpp: prompt_token_len = %d", prompt_token_len);
    return prompt_token_len;
}

int LLM::completion_init(const char* text, bool format_chat, int n_len) {
    cached_token_chars.clear();

    std::vector<char> formatted(llama_n_ctx(context));
    const auto model = llama_get_model(context);
    const llama_vocab * vocab = llama_model_get_vocab(model);
    const char * tmpl = llama_model_chat_template(model, /* name */ nullptr);
    messages.push_back({"user", strcpy(new char[strlen(text) + 1], text)});
    int new_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    if (new_len > (int)formatted.size()) {
        formatted.resize(new_len);
        new_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
    }
    if (new_len < 0) {
        LOGe("failed to apply the chat template\n");
    }
    std::string prompt(formatted.begin() + chat_history_tokens_len, formatted.begin() + new_len);
    LOGi("Current Chat History (Size: %zu):", messages.size());
    for (size_t i = 0; i < messages.size(); ++i) {
        const llama_chat_message& msg_to_log = messages[i];
        const char* role_to_log = msg_to_log.role ? msg_to_log.role : "[null role]";
        const char* content_to_log = msg_to_log.content ? msg_to_log.content : "[null content]";
        LOGi("  Msg %zu: Role: \"%s\", Content: \"%s\"", i, role_to_log, content_to_log);
    }
    LOGi("prompt:%s",prompt.c_str());

    bool parse_special = format_chat;
    const auto tokens_list = common_tokenize(context, prompt, true, parse_special);

    auto n_ctx = llama_n_ctx(context);
    int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(context), 0);
    auto n_kv_req = tokens_list.size() + n_len + n_ctx_used;

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

    for (auto i = 0; i < tokens_list.size(); i++) {
        common_batch_add(*batch, tokens_list[i], i+pos, { 0 }, false);
    }

    batch->logits[batch->n_tokens - 1] = true;

    if (llama_decode(context, *batch) != 0) {
        LOGe("llama_decode() failed");
    }

    prev_tokens_len += batch->n_tokens;
    return batch->n_tokens;
}

std::string LLM::completion_loop(int n_len) {
    const auto model = llama_get_model(context);
    const auto vocab = llama_model_get_vocab(model);

    const auto new_token_id = llama_sampler_sample(sampler, context, -1);

    if (llama_vocab_is_eog(vocab, new_token_id) ) {
        LOGi("DONE Loop retrun nullptr, n_len:%d" ,n_len);
        return "";
    }

    auto new_token_chars = common_token_to_piece(context, new_token_id);
    cached_token_chars += new_token_chars;

    if (is_valid_utf8(cached_token_chars.c_str())) {
        LOGi("cached: %s, new_token_chars: `%s`, id: %d", cached_token_chars.c_str(), new_token_chars.c_str(), new_token_id);
        std::string result = cached_token_chars;
        cached_token_chars.clear();
        
        common_batch_clear(*batch);
        common_batch_add(*batch, new_token_id, prev_tokens_len, {0 }, true);

        prev_tokens_len++;

        if (llama_decode(context, *batch) != 0) {
            LOGe("llama_decode() returned null");
        }

        return result;
    } else {
        return "";
    }
}

void LLM::kv_cache_clear() {
    llama_memory_clear(llama_get_memory(context), true);
    prev_tokens_len=0;
    chat_history_tokens_len = 0;
    messages.clear();
}

void LLM::supply(const char* text) {
    messages.push_back({"assistant", strcpy(new char[strlen(text) + 1], text)});
    const char * tmpl = llama_model_chat_template(model, /* name */ nullptr);
    chat_history_tokens_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), false, nullptr, 0);
    if (chat_history_tokens_len < 0) {
        LOGe("failed to apply the chat template\n");
    }
}

bool LLM::is_valid_utf8(const char * string) {
    if (!string) {
        return true;
    }

    const unsigned char * bytes = (const unsigned char *)string;
    int num;

    while (*bytes != 0x00) {
        if ((*bytes & 0x80) == 0x00) {
            num = 1;
        } else if ((*bytes & 0xE0) == 0xC0) {
            num = 2;
        } else if ((*bytes & 0xF0) == 0xE0) {
            num = 3;
        } else if ((*bytes & 0xF8) == 0xF0) {
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

void LLM::log_callback(ggml_log_level level, const char * fmt, void * data) {
    if (level == GGML_LOG_LEVEL_ERROR)     fprintf(stderr, "ERROR: %s\n", fmt);
    else if (level == GGML_LOG_LEVEL_INFO) fprintf(stdout, "INFO: %s\n", fmt);
    else if (level == GGML_LOG_LEVEL_WARN) fprintf(stdout, "WARN: %s\n", fmt);
    else fprintf(stdout, "%s\n", fmt);
}

void LLM::backend_init() {
    llama_backend_init();
}

void LLM::backend_free() {
    llama_backend_free();
}

void LLM::log_to_console() {
    llama_log_set(log_callback, NULL);
}

llama_context* LLM::new_context(llama_model* model) {
    if (!model) {
        LOGe("new_context(): model cannot be null");
        return nullptr;
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
        return nullptr;
    }

    return context;
}

void LLM::free_model(llama_model* model) {
    llama_model_free(model);
}

llama_batch* LLM::new_batch(int n_tokens, int embd, int n_seq_max) {
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

    return batch;
}

void LLM::free_batch(llama_batch* batch) {
    delete batch;
}

llama_sampler* LLM::new_sampler() {
    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = true;
    llama_sampler * smpl = llama_sampler_chain_init(sparams);
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.6f));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(20));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.95,1));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    return smpl;
}

void LLM::free_sampler(llama_sampler* sampler) {
    llama_sampler_free(sampler);
}

void LLM::free_context(llama_context* context) {
    llama_free(context);
}