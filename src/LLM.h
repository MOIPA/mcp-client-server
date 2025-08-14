#ifndef LLM_H
#define LLM_H

#include <string>
#include <vector>
#include "llama.h"
#include "mtmd.h"

class LLM {
public:
    LLM();
    ~LLM();

    bool load(const std::string& model_path, const std::string& mmproj_path, int gpu_layers);
    void unload();
    std::string send(const std::string& user_input, const std::string& image_path = "");

private:
    llama_model* model;
    llama_context* context;
    llama_batch* batch;
    llama_sampler* sampler;

    // Vision model members
    mtmd::context_ptr ctx_vision;
    mtmd::bitmaps bitmaps;

    // Internal state
    std::vector<llama_chat_message> messages;
    int prev_tokens_len;
    int chat_history_tokens_len;
    std::string cached_token_chars;


    // Internal helper functions
    void init_vision_context(const char * mmprojPath,int gpu,llama_model * model,int verbosity=0);
    bool load_media(const char * fname);
    bool check_vision_ready();
    int completion_init_vision(const char* text, bool format_chat, int n_len, const char* picf);
    int completion_init(const char* text, bool format_chat, int n_len);
    std::string completion_loop(int n_len);
    void kv_cache_clear();
    void supply(const char* text);

    // Static helper functions
    static bool is_valid_utf8(const char * string);
    static void log_callback(ggml_log_level level, const char * fmt, void * data);
    static void backend_init();
    static void backend_free();
    static void log_to_console();
    static llama_context* new_context(llama_model* model);
    static void free_model(llama_model* model);
    static llama_batch* new_batch(int n_tokens, int embd, int n_seq_max);
    static void free_batch(llama_batch* batch);
    static llama_sampler* new_sampler();
    static void free_sampler(llama_sampler* sampler);
    static void free_context(llama_context* context);
};

#endif // LLM_H