# llama.cpp

对llamacpp项目的学习和理解

移动端使用的设备是努比亚手机，需要调整默认的日志输出级别：
`adb shell setprop persist.log.tag V`

android系统日志等级有 VERBOSE（V）>DEBUG（D）>INFO（I）>WARN（W）>ERROR（E）>FATA（F）>SILENT（S）

注意：移动端设备GPU加速前下载aida64或者cpuz查看移动端设备是否支持vulkan或者openCL，选择对应的编译

## linux下编译安装

使用了源码安装，系统newstartos，gcc和g++是8.4.1

1. 安装ccache，命令cp到usr/bin就行

2. git clone https://github.com/ggml-org/llama.cpp 然后 cd llama.cpp

3. echo 'target_link_libraries(ggml PRIVATE stdc++fs)' >> CMakeLists.txt  ： gcc9.0之后才默认并入stdc++fs，在这之前的版本都需要手动引入库
4. cmake -S . -B build -DCMAKE_EXE_LINKER_FLAGS="-lstdc++fs"
5. cmake --build build --config Release

6. 编译完成后命令都在build/bin下，默认不会make install，可以手动cp到usr/local/bin

7. 可选安装：sudo cp build/bin /usr/local/bin

## android ndk编译和使用

使用android-studio提供的ndk，cmake编译链工具预编译移动平台的动态链接库，android项目中引入动态链接库通过JNI方式调用完成模型核心功能部署。

### 编译

命令(都是从android studio下载的)：

```
export ANDROID_NDK=/home/0668001490/Android/Sdk/ndk/29.0.13599879

# -DCMAKE_TOOLCHAIN_FILE  这个是使用ndk平台编译的意思，不写就用linux平台g++和依赖库编译了
# 写入配置文件
/home/0668001490/Android/Sdk/cmake/4.0.2/bin/cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DCMAKE_C_FLAGS="-march=armv8.7a"  -DCMAKE_CXX_FLAGS="-march=armv8.7a" -DGGML_OPENMP=OFF -DGGML_LLAMAFILE=OFF -DLLAMA_CURL=OFF -B build-android

# 开始编译  4线程执行
/home/0668001490/Android/Sdk/cmake/4.0.2/bin/cmake --build build-android --config Release -j 4

# 安装编译后内容
# 它的作用是将项目构建过程中生成的特定文件（例如可执行文件、库文件、头文件、文档等）从构建目录 (build-android) 复制到一个用户指定的安装目录 ({install-dir})
cmake --install build-android --prefix {install-dir} --config Release
```

编译的结果默认是基于arm64-v8a的，编译时已经指定了参数`-march=armv8.7a`

> install-dir中内容

1. bin：存放android下可执行文件
2. include: 核心功能的所有头文件（没有common.h）
3. lib：动态链接库，没有libcommon.a静态库

官方的示例中还是使用了common.h内的一些函数，如果想使用，得去build-android/common内找到缺失的libcommon.a静态库，并添加common.h头文件才能在android项目中使用common_的函数

### GPU加速

比较常见的两种，openCL和vulkan

#### openCL

基于openCL版本的编译

本机的cmake版本太低，使用android-studio提供的：`export CHOME=/home/0668001490/Android/Sdk/cmake/4.0.2/bin/`

1. 编译拷贝需要的openCL依赖头和loader

    ```
    mkdir llm
    cd llm
    git clone https://github.com/KhronosGroup/OpenCL-Headers
    cd OpenCL-Headers
    cp -r CL $ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include
    ls $ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/CL
    cd ..
    git clone https://github.com/KhronosGroup/OpenCL-ICD-Loader
    cd OpenCL-ICD-Loader
    mkdir build_ndk && cd build_ndk
    
    $CHOME/cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DOPENCL_ICD_LOADER_HEADERS_DIR=$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=24 -DANDROID_STL=c++_shared 
    
    ninja 

    cp libOpenCL.so $ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android
    ```

2. 编译llama.cpp依赖库，开启openCL

    ```
    cd llama.cpp
    mkdir build-android-openCL && cd build-android-openCL

    $CHOME/cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DBUILD_SHARED_LIBS=ON -DGGML_OPENCL=ON -DLLAMA_CURL=OFF -DCMAKE_C_FLAGS="-march=armv8.7a" -DCMAKE_CXX_FLAGS="-march=armv8.7a" -DGGML_OPENMP=OFF -DGGML_LLAMAFILE=OFF

    ninja
    cmake --install ./ --prefix ./android-res --config Release\n
    ```
3. 从安装的android-res和build目录下拷贝动态链接库和静态库linbcommon.a到安卓项目下替换原来的库

注意：llama.cpp编译的openCL功能相关的后端依赖库libggml-opencl.so依赖android底层的openCL驱动：libOpenCL.so，而android系统为了安全和稳定限制了能访问的系统库，需要手动指定需要加载的库，修改AndroidManifest.xml文件

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    xmlns:tools="http://schemas.android.com/tools">

    <application
        android:allowBackup="true"
        android:dataExtractionRules="@xml/data_extraction_rules"
        ......
        <uses-native-library android:name="libOpenCL.so" android:required="true"/>
    </application>

</manifest>
```

OpenCL backend 对Q_8量化比较差目前，推理速度不如CPU

#### vulkan

vulkan需要设置vulkanSDK，依赖最新的glibc和gcc编译器。本机电脑的glibc和gcc都比较老，需要更新

> vulkanSDK设置较为简单，下载解压缩添加LD_LIBRARY_PATH即可，参考官方文档

需要注意的是如果glibc编译后报错ld.so的错误，说明编译的gcc版本不够，需要先编译gcc再编译glibc，编译完成gcc后`export CC=/opt/gcc-10.1/gcc`,`export CXX=/opt/gcc-10.1/g++`

1. 更新glibc，无法更新系统，只能手动编译，编译的包放在/opt下，需要的程序手动指定`ld_path`

    ```bash
    # 下载glibc 2.29 版本，解压缩
    mkdir glibc-build
    # 配置
    ../glibc-2.29/configure --prefix=/opt/glibc-2.29 --disable-profile --enable-add-ons --with-headers=/usr/include --enable-obsolete-nsl
    # 编译
    make -j$(nproc)
    # 安装
    sudo make install
    # 使用
    # 在~/.zshrc内export LD_LIBRARY_PATH="/opt/glibc-2.29/lib:$LD_LIBRARY_PATH"
    # 这种方式对全局有效，但是如果编译的结果只被部分安装的话，还是手动指定应用程序吧，如下
    LD_LIBRARY_PATH="/opt/glibc-2.29/lib:$LD_LIBRARY_PATH" /opt/vulkan-1.4/x86_64/bin/glslc
    ```

2. 更新GCC，方式同上
    ```bash
    mkdir gcc-build && cd gcc-build
    # 我的os的依赖库
    sudo dnf install gmp-devel
    sudo dnf install mpfr-devel
    sudo dnf install libmpc-devel
    # 编译64和32位
    ../gcc-10.1.0/configure --prefix=/opt/gcc-10.1
    # 只编译64位
    ../gcc-10.1.0/configure --prefix=/opt/gcc-10.1 --disable-multilib
    make -j$(nproc)
    sudo make install
    export LD_LIBRARY_PATH="/opt/gcc-10.1/lib64:$LD_LIBRARY_PATH"
    ```

3. 进入llama.cpp项目，创建build文件夹：`mkdir build-vulkan&&cd build-vulkan`，然后执行CMake配置：`$CHOME/cmake ..  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DBUILD_SHARED_LIBS=ON -DGGML_VULKAN=ON -DLLAMA_CURL=OFF -DCMAKE_C_FLAGS="-march=armv8.7a" -DCMAKE_CXX_FLAGS="-march=armv8.7a" -DGGML_OPENMP=OFF -DGGML_LLAMAFILE=OFF`，主要是开启VULKAN=ON 其他参数不变

4. 执行编译：`$CHOME/cmake --build . --config Release -j4`

5. install到本地目录方便拷贝头文件和动态库：`cmake --install ./ --prefix ./android-res --config Release`

### android JNI使用

编译成功后，现在拥有了可以直接在 Android JNI 项目中使用 .so 共享库文件，而不需要再像之前那样将 llama.cpp 的全部源码拷贝到你的项目中并尝试在 Android Studio 的构建系统（如 CMake 或 ndk-build）中重新编译

补充说明：官方示例会用到common包里的函数，但是common包编译的默认是静态库，搜索下编译的结果找到libcommon.a正常引入

> 在一个android项目中

0. 拷贝ndk编译好的所有so和a库到：`src/main/jniLibs/arm64-v8a`，android会自动寻找jniLibs，并根据手机平台决定使用哪个ABI，现在手机基本都是arm64位架构的，固定`arm64-v8a`就行
1. 拷贝模型文件到:`src/main/assets/qwen.gguf`
2. 拷贝可能会用到的头文件至：`src/main/cpp/include/llama`
3. 编写CMakeLists.txt文件：`src/main/cpp/CMakeLists.txt` ,内容如下：

    ```
    cmake_minimum_required(VERSION 3.18.1)

    project("woof") # 你的JNI项目的名字

    # 1. 设置头文件包含目录
    #    llama.cpp 的头文件 (llama.h, ggml.h 等) 复制到了
    #    src/main/cpp/include/llama_sdk/ 目录下
    include_directories(
            ${CMAKE_CURRENT_SOURCE_DIR}/include/llama # 指向你拷贝头文件的路径
    )

    # 2. 添加 JNI 封装库
    add_library(
            woof # 通过 System.loadLibrary("woof") 加载的 JNI 库的名字
            SHARED
            ChatJNIWrapper.cpp # JNI C++源文件
    )

    # 3. 查找并链接预编译的共享库
    #    这些 .so 文件应该位于你的 jniLibs/<ABI>/ 目录下，例如 src/main/jniLibs/arm64-v8a/

    #    设置预编译库所在的目录。
    #    这个路径是相对于你的模块的 src/main/ 目录。
    #    确保 Gradle 的 android.sourceSets.main.jniLibs.srcDirs 配置正确。
    #    通常默认是 ['src/main/jniLibs']
    set(PREBUILT_LIBS_DIR ${CMAKE_SOURCE_DIR}/../jniLibs/${CMAKE_ANDROID_ARCH_ABI})

    #    为 libllama.so 创建一个 IMPORTED 库目标
    add_library(llama_shared SHARED IMPORTED GLOBAL) # GLOBAL 可选，但有时有帮助
    set_target_properties(llama_shared PROPERTIES
            IMPORTED_LOCATION ${PREBUILT_LIBS_DIR}/libllama.so
    )

    #    为 libggml.so 创建一个 IMPORTED 库目标
    add_library(ggml_shared SHARED IMPORTED GLOBAL)
    set_target_properties(ggml_shared PROPERTIES
            IMPORTED_LOCATION ${PREBUILT_LIBS_DIR}/libggml.so
    )

    #    为 libggml-base.so 创建一个 IMPORTED 库目标
    add_library(ggml_base_shared SHARED IMPORTED GLOBAL)
    set_target_properties(ggml_base_shared PROPERTIES
            IMPORTED_LOCATION ${PREBUILT_LIBS_DIR}/libggml-base.so
    )

    #    为 libggml-cpu.so 创建一个 IMPORTED 库目标
    add_library(ggml_cpu_shared SHARED IMPORTED GLOBAL)
    set_target_properties(ggml_cpu_shared PROPERTIES
            IMPORTED_LOCATION ${PREBUILT_LIBS_DIR}/libggml-cpu.so
    )

    #    为 libmtmd.so 创建一个 IMPORTED 库目标 (如果存在)
    #    请确认 libmtmd.so 是否确实是必须的，以及它的确切用途和来源。
    #    如果它只是 libllama.so 或 libggml*.so 的内部依赖，并且都在 jniLibs 目录中，
    #    运行时链接器会自动处理，你可能不需要在这里显式为它创建 IMPORTED 目标并链接。
    #    但为了编译时符号解析，显式链接有时更安全。
    add_library(mtmd_shared SHARED IMPORTED GLOBAL)
    set_target_properties(mtmd_shared PROPERTIES
            IMPORTED_LOCATION ${PREBUILT_LIBS_DIR}/libmtmd.so
    )

    #    链接你的 JNI 库 (woof) 到所有需要的共享库
    target_link_libraries(
            woof
            PRIVATE # 或者 PUBLIC，取决于你的需求和头文件暴露程度
            llama_shared
            ggml_shared
            ggml_base_shared
            ggml_cpu_shared
            mtmd_shared      # 如果你确定需要显式链接它
            log              # Android 日志库
            # atomic         # 如果确实需要，可以取消注释
            # m              # 数学库，通常会被隐式链接，但显式写出也无妨
    )
    # 确保在你的 build.gradle (Module: app) 文件中，你有类似这样的配置
    # android {
    #    defaultConfig {
    #    applicationId = "com.example.woof"
    #    minSdk = 28
    #    ....
    #    externalNativeBuild {
    #        cmake {
    #            cppFlags += ""
    #        }
    #    }
    #   }
    #    ...
    #    externalNativeBuild {
    #        cmake {
    #            path = file("src/main/cpp/CMakeLists.txt")
    #            version = "3.22.1"
    #        }
    #    }
    # }

    ```
4. 编写调用头文件函数，编写模型交互功能：`src/main/cpp/ChatJNIWrapper.cpp`:

    ```c
    #include <jni.h>
    #include <string>
    #include <vector>
    #include <android/log.h> // For Android logging
    #include "llama.h"       // llama.h must be accessible

    #define TAG "ChatJNIWrapper_Simple"
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

    static llama_model *g_model = nullptr;
    static llama_context *g_ctx = nullptr;
    static llama_sampler *g_smpl = nullptr; // Added sampler
    static const llama_vocab *g_vocab = nullptr; // Added vocab

    void throw_java_exception(JNIEnv* env, const char* class_name, const char* message) {
        jclass exClass = env->FindClass(class_name);
        if (exClass != nullptr) {
            env->ThrowNew(exClass, message);
        }
    }
    extern "C" JNIEXPORT jboolean JNICALL
    Java_com_example_woof_LlamaCppBridge_nativeInit(
            JNIEnv *env,
            jobject /* this */,
            jstring model_path_jni) {
        if (g_smpl) { llama_sampler_free(g_smpl); g_smpl = nullptr; }
        if (g_ctx) { llama_free(g_ctx); g_ctx = nullptr; }
        if (g_model) { llama_model_free(g_model); g_model = nullptr; }
        g_vocab = nullptr;
        const char *c_model_path = env->GetStringUTFChars(model_path_jni, nullptr);
        if (c_model_path == nullptr) {
            LOGE("Failed to get model path string.");
            throw_java_exception(env, "java/io/IOException", "Failed to get model path string.");
            return JNI_FALSE;
        }
        std::string model_path_str = c_model_path;
        env->ReleaseStringUTFChars(model_path_jni, c_model_path);
        LOGI("Initializing Llama model from: %s", model_path_str.c_str());
        llama_log_set([](ggml_log_level level, const char *text, void * /*user_data*/) {
            if (level >= GGML_LOG_LEVEL_WARN) { // Log warnings and errors
                __android_log_print(level == GGML_LOG_LEVEL_ERROR ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN, "llama.cpp", "%s", text);
            }
        }, nullptr);

        llama_backend_init(); // For newer llama.cpp with llama_backend_init(numa)
        llama_model_params model_params = llama_model_default_params();

        g_model = llama_model_load_from_file(model_path_str.c_str(), model_params);
        if (!g_model) {
            LOGE("Error: unable to load model at %s", model_path_str.c_str());
            throw_java_exception(env, "java/io/IOException", "Unable to load llama model.");
            return JNI_FALSE;
        }
        LOGI("Model loaded successfully.");

        g_vocab = llama_model_get_vocab(g_model);
        if (!g_vocab) {
            LOGE("Error: Failed to get vocabulary from model.");
            llama_model_free(g_model); g_model = nullptr;
            throw_java_exception(env, "java/lang/RuntimeException", "Failed to get vocabulary.");
            return JNI_FALSE;
        }
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 512;   // Reduced context size for simple demo
        ctx_params.n_batch = 512; // Should generally be <= n_ctx

        g_ctx = llama_init_from_model(g_model, ctx_params);
        if (!g_ctx) {
            LOGE("Error: failed to create the llama_context.");
            llama_model_free(g_model); g_model = nullptr;
            throw_java_exception(env, "java/lang/RuntimeException", "Failed to create llama context.");
            return JNI_FALSE;
        }
        LOGI("Context created successfully.");

        g_smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
        if (!g_smpl) {
            LOGE("Error: failed to create sampler chain.");
            llama_free(g_ctx); g_ctx = nullptr;
            llama_model_free(g_model); g_model = nullptr;
            throw_java_exception(env, "java/lang/RuntimeException", "Failed to create sampler chain.");
            return JNI_FALSE;
        }
        llama_sampler_chain_add(g_smpl, llama_sampler_init_min_p(0.05f, 1));
        llama_sampler_chain_add(g_smpl, llama_sampler_init_temp(0.8f)); // Slightly lower temp for more coherent short output

        llama_sampler_chain_add(g_smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED)); // Use default seed
        LOGI("Sampler created and configured.");

        return JNI_TRUE;
    }

    extern "C" JNIEXPORT jstring JNICALL
    Java_com_example_woof_LlamaCppBridge_nativeGenerate(
            JNIEnv *env,
            jobject /* this */,
            jstring prompt_jni) {
        llama_kv_self_clear(g_ctx);
        if (!g_model || !g_ctx || !g_smpl || !g_vocab) {
            LOGE("Generation called before successful initialization or after release.");
            throw_java_exception(env, "java/lang/IllegalStateException", "Llama not initialized or already released.");
            return nullptr;
        }

        const char *c_prompt = env->GetStringUTFChars(prompt_jni, nullptr);
        if (c_prompt == nullptr) {
            LOGE("Failed to get prompt string.");
            throw_java_exception(env, "java/lang/RuntimeException", "Failed to get prompt string.");
            return nullptr;
        }
        std::string prompt_str = c_prompt;
        env->ReleaseStringUTFChars(prompt_jni, c_prompt);
        LOGI("Generating for prompt: %s", prompt_str.c_str());
        std::string response_str;
        const int max_tokens_to_generate = 64; // Limit for this demo
        int tokens_generated = 0;
        const bool is_first_eval = llama_memory_seq_pos_max(llama_get_memory(g_ctx), 0) == -1;

        const int n_prompt = -llama_tokenize(g_vocab, prompt_str.c_str(), prompt_str.size(), NULL, 0, true, true);
        std::vector<llama_token> prompt_tokens(n_prompt);
        LOGI("Prompt tokenized into %d tokens.", n_prompt);
        int n_prompt_tokens = llama_tokenize(g_vocab, prompt_str.c_str(), prompt_str.length(), prompt_tokens.data(), prompt_tokens.size(), is_first_eval, true);
        if ( n_prompt_tokens < 0) {
            LOGE("Failed to tokenize prompt (buffer too small or other error). n_prompt_tokens: %d", n_prompt_tokens);
            throw_java_exception(env, "java/lang/RuntimeException", "Failed to tokenize prompt.");
            return nullptr;
        }
        llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
        llama_token new_token_id;
        while (tokens_generated < max_tokens_to_generate) {
            int n_ctx_val = llama_n_ctx(g_ctx);
            int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(g_ctx), 0); // Current sequence length
            if (n_ctx_used + batch.n_tokens > n_ctx_val) {
                LOGE("Context size exceeded during generation. Used: %d, Batch: %d, Max: %d", n_ctx_used, batch.n_tokens, n_ctx_val);
                break;
            }
            if (llama_decode(g_ctx, batch) != 0) { // 0 on success
                LOGE("llama_decode failed.");
                throw_java_exception(env, "java/lang/RuntimeException", "llama_decode failed.");
                return nullptr;
            }
            new_token_id = llama_sampler_sample(g_smpl, g_ctx, -1); // -1 means use the last token of the current sequence
            if (llama_vocab_is_eog(g_vocab, new_token_id)) {
                LOGI("EOG token reached.");
                break;
            }
            char buf[256]; // Buffer for the token piece
            int n_chars = llama_token_to_piece(g_vocab, new_token_id, buf, sizeof(buf), 0, true);
            if (n_chars < 0) {
                LOGE("Failed to convert token to piece.");
                throw_java_exception(env, "java/lang/RuntimeException", "Failed to convert token to piece.");
                return nullptr;
            }
            std::string piece(buf, n_chars);
            response_str += piece;
            tokens_generated++;
            batch = llama_batch_get_one(&new_token_id, 1);
        }
        LOGI("Generated response: %s", response_str.c_str());
        return env->NewStringUTF(response_str.c_str());
    }
    extern "C" JNIEXPORT void JNICALL
    Java_com_example_woof_LlamaCppBridge_nativeRelease(
            JNIEnv *env,
            jobject /* this */) {
        LOGI("Releasing Llama resources.");
        if (g_smpl) {
            llama_sampler_free(g_smpl);
            g_smpl = nullptr;
            LOGI("Sampler freed.");
        }
        if (g_ctx) {
            llama_free(g_ctx);
            g_ctx = nullptr;
            LOGI("Context freed.");
        }
        if (g_model) {
            llama_model_free(g_model);
            g_model = nullptr;
            LOGI("Model freed.");
        }
        g_vocab = nullptr; // Reset vocab pointer
        LOGI("Llama resources released.");
    }

    ```

5. 编写功能函数cpp的kotlin映射调用文件：`src/main/java/com/example/woof`:

    ```java
    package com.example.woof 
    import android.util.Log
    object LlamaCppBridge {
        private const val TAG = "LlamaCppBridge_Simple"
        external fun nativeInit(modelPath: String): Boolean
        external fun nativeGenerate(prompt: String): String? // Null if error
        external fun nativeRelease()
        init {
            try {
                System.loadLibrary("woof") // Name from CMakeLists.txt add_library()
                Log.i(TAG, "Native library 'woof' loaded successfully.")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Failed to load native library 'woof'", e)
                throw e 
            }
        }
    }
    ```

6. 在一个ViewModel中调用kotlin内的模型函数功能：`src/main/java/com/example/woof/ui/ViewModel.kt`

    ```java
        class LLMViewModel(private val applicationContext: Context) : ViewModel() {
            var llamaPromptInput by mutableStateOf("") // State for Llama's input field
            private set

            fun updateLlamaPromptInput(newPrompt: String) {
                llamaPromptInput = newPrompt
            }

            private val TAG_LLM = "GameViewModel_LLM"

            var outputTextLlm by mutableStateOf("Initializing LLM...")
                private set

            var currentModelName by mutableStateOf("Smol-256M")
                private set

            private var isLlmInitialized = false

            // Llama 初始化
            init { // 类的初始化代码块，它会在类被创建（实例化）时自动执行一次
                initializeLlama()
            }

            private fun initializeLlama() {
                viewModelScope.launch {
                    outputTextLlm =
                        "Copying model and initializing Llama..." 
                    val success = withContext(Dispatchers.IO) {
                        try {
                            // 默认在assets目录下 拷贝到自己应用空间内
                            val modelName = "qwen.gguf" 
                            val modelFile = File(applicationContext.filesDir, modelName)
                            if (!modelFile.exists()) {
                                Log.d(TAG_LLM, "Model file does not exist. Copying from assets...")
                                try {
                                    applicationContext.assets.open(modelName).use { inputStream ->
                                        FileOutputStream(modelFile).use { outputStream ->
                                            inputStream.copyTo(outputStream)
                                        }
                                    }
                                    Log.d(TAG_LLM, "Model copied to ${modelFile.absolutePath}")
                                } catch (e: Exception) {
                                    Log.e(TAG_LLM, "Error copying model from assets: ${e.message}", e)
                                    outputTextLlm =
                                        "Error: Could not copy model. Place '$modelName' in app/src/main/assets/"
                                    return@withContext false
                                }
                            } else {
                                Log.d(TAG_LLM, "Model file already exists at ${modelFile.absolutePath}")
                            }

                            // --- 初始化模型
                            Log.d(TAG_LLM, "Calling nativeInit with path: ${modelFile.absolutePath}")
                            LlamaCppBridge.nativeInit(modelFile.absolutePath) // This should return boolean
                        } catch (e: Exception) {
                            Log.e(TAG_LLM, "Error during Llama initialization: ${e.message}", e)
                            outputTextLlm = "Error initializing Llama: ${e.message}"
                            false // Return false from withContext block
                        }
                    }

                    if (success) {
                        isLlmInitialized = true
                        outputTextLlm = "Llama initialized. Ready to generate."
                        Log.d(TAG_LLM, "Llama nativeInit successful.")
                    } else {
                        isLlmInitialized = false
                        Log.e(TAG_LLM, "Llama nativeInit failed.")
                        if (outputTextLlm.startsWith("Initializing") || outputTextLlm.startsWith("Copying")) {
                            outputTextLlm = "Error: Llama initialization failed. Check Logcat for details."
                        }
                    }
                }
            }

            fun generateLlmText(prompt: String) { 
                // 手动添加了一个模版 这个模版只和qwen系列模型适配
                val systemMessage = "You are a helpful assistant."
                val formatedPrompt = """
                <|im_start|>system
                $systemMessage<|im_end|>
                <|im_start|>user
                $prompt<|im_end|>
                <|im_start|>assistant
                """.trimIndent()

                if (!isLlmInitialized) {
                    outputTextLlm = "Error: Llama not initialized."
                    Log.w(TAG_LLM, "generateLlmText called but Llama not initialized.")
                    return
                }
                if (prompt.isBlank()) {
                    outputTextLlm = "Please enter a prompt for LLM."
                    return
                }

                outputTextLlm = "Generating LLM text..."
                viewModelScope.launch {
                    val result =
                        withContext(Dispatchers.Default) { 
                            try {
                                Log.d(TAG_LLM, "Calling nativeGenerate with prompt: $prompt")
                                LlamaCppBridge.nativeGenerate(formatedPrompt) 
                            } catch (e: Exception) {
                                Log.e(TAG_LLM, "Error during Llama generation: ${e.message}", e)
                                "Error generating LLM text: ${e.message}"
                            }
                        }
                    outputTextLlm = result ?: "LLM Generation returned null (error or no output)."
                    Log.d(TAG_LLM, "LLM Generation result: $outputTextLlm")
                }
            }

            override fun onCleared() {
                super.onCleared()
                if (isLlmInitialized) {
                    viewModelScope.launch(Dispatchers.IO) { 
                        Log.d(TAG_LLM, "ViewModel cleared. Releasing Llama resources.")
                        try {
                            LlamaCppBridge.nativeRelease() 
                            isLlmInitialized = false
                        } catch (e: Exception) {
                            Log.e(TAG_LLM, "Error releasing Llama resources: ${e.message}", e)
                        }
                    }
                }
                Log.d("GameViewModel", "GameViewModel Cleared") // General log for ViewModel clear
            }
        }
        // You NEED this Factory to create GameViewModel with a Context parameter
        class LLMViewModelFactory(private val context: Context) : ViewModelProvider.Factory {
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                if (modelClass.isAssignableFrom(LLMViewModel::class.java)) {
                    @Suppress("UNCHECKED_CAST")
                    return LLMViewModel(context.applicationContext) as T // Pass applicationContext
                }
                throw IllegalArgumentException("Unknown ViewModel class")
            }
        }
    ```
7. 对应的UI界面中和ViewModel的变量以及函数交互：`src/main/java/com/example/woof/ui/Screen.kt`

    ```java
        @Composable
        fun GameScreen(llmViewModel: LLMViewModel = viewModel(
            factory = LLMViewModelFactory(LocalContext.current.applicationContext)
        )) {  
            Column(
                modifier = Modifier
                    .statusBarsPadding()
                    .verticalScroll(rememberScrollState())
                    .safeDrawingPadding()
                    .padding(mediumPadding),
                verticalArrangement = Arrangement.Center,
                horizontalAlignment = Alignment.CenterHorizontally
            ) {

                Text(
                    text = stringResource(R.string.app_name),
        //            text= stringFromJNI(),
                    style = typography.titleLarge,
                )
                GameLayout(
                    currentModelName = llmViewModel.currentModelName,
                    modifier = Modifier
                        .fillMaxWidth()
                        .wrapContentHeight()
                        .padding(mediumPadding),
                    outputTextLlm = llmViewModel.outputTextLlm
                )
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(mediumPadding),
                    verticalArrangement = Arrangement.spacedBy(mediumPadding),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Button(
                        modifier = Modifier.fillMaxWidth(),
                        onClick = {
                            llmViewModel.generateLlmText(gameViewModel.userGuess)
                            llmViewModel.updateUserGuess("")
                        }
                    ) {
                        Text(
                            text = stringResource(R.string.submit),
                            fontSize = 16.sp
                        )
                    }
                }
            }
        }
        @Composable
        fun GameLayout(
            currentModelName:String,modifier: Modifier = Modifier,
            outputTextLlm: String){
            val mediumPadding = dimensionResource(R.dimen.padding_medium)
                Card(
                    modifier = modifier,
                    elevation = CardDefaults.cardElevation(defaultElevation = 5.dp)
                ) {
                    Column(
                        verticalArrangement = Arrangement.spacedBy(mediumPadding),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        modifier = Modifier.padding(mediumPadding)
                    ) {
                        Text(
                            text = currentModelName,
                            style = typography.displayMedium
                        )
                        Text(
                            text = outputTextLlm, // Display the Llama output state
                            style = typography.bodyMedium,
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 8.dp)
                        )
                    }
                }
            }
    ```

8. 运行android程序，编译的时候可能报错内存不足，因为打包到apk的资产中的模型比较大，增加配置的空间就行

## 命令行/C代码 基本使用

1. 去hg下载几个gguf格式的量化模型

2. 加载：`llama-cli -m qwen2.5-1.5b-instruct-q2_k.gguf `

`llama-cli` 这个指令我查阅了源码，位置在tools/main/main.cpp，作者是将这个main文件编译成可执行文件放入build/bin下，基本所有命令都是这么来的，可以详细看看怎么编写的，算是一个展示怎么使用各种函数加载和模型推理的非常良好的示例。

### 基于C调用

llamacpp编译完成后会在build/bin目录下生成若干指令和lib的so动态链接库，完全可以直接调用动态链接库进行编程

### 项目结构

> include目录的头文件是直接从llama.cpp的include和src里面找到并拷贝下来的，lib内文件是本地编译的

```
project/
├── src/
│   └── main.cpp           // 使用llama来加载模型推理的代码
├── include/
│   ├── llama.h            // llama.cpp 的头文件,本来只要一个llama.h但是层层依赖就这么多头文件了
│   ├── common.h            
│   ├── ggml-backend.h            
│   ├── ggml-cpu.h            
│   ├── ggml-opt.h            
│   ├── ggml.h            
│   ├── ggml-alloc.h                      
│   └── llama-cpp.h            
├── lib/
│   ├── libllama.so        // llama.cpp 的动态库（Linux）都是从llama.cpp官方文档指导下在本地编译的so文件
│   ├── libllama.so        
│   ├── libllama.so        
│   ├── libllama.so        
    └── libggml-base.so
└── model/
    └── qwen.gguf          // 我下载的一个qwen2.5 1.5b q8_0的gguf本地模型
```

### 代码内容

> 官方也提供了示例，在llama.cpp内的example下

main.c
```c
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
```

### 编译并链接so库


#### 手动编译

1. 我在目录下的src目录进行的编译：`g++ -I../include -L../lib -Wl,-rpath=../lib -o llama_demo main.cpp -pthread -lm ../lib/*.so`

> 参数意义
+ g++	GNU C++ 编译器，用于编译和链接 C++ 程序
+ -I../include	添加头文件搜索路径，告诉编译器去 ../include 目录下找 .h 或 .hpp 文件
+ -L../lib	添加库文件搜索路径，告诉链接器去 ../lib 目录下找 .so（Linux）或 .a/.dll（Windows）库文件
+ -Wl,-rpath=../lib	传递链接器选项：告诉生成的可执行文件在运行时去 ../lib 路径下查找 .so 动态库文件，避免每次运行前手动设置 LD_LIBRARY_PATH
+ -o llama_demo	指定输出的可执行文件名为 llama_demo
+ main.cpp	要编译的源文件
+ -pthread	启用 POSIX 线程支持（多线程）
+ -lm	链接数学库（libm.so），提供 sin, cos, sqrt 等数学函数
+ ../lib/*.so	匹配并链接 ../lib 目录下所有 .so 文件（如 libggml.so, libllama.so）

2. 编译后执行就行：`./llama_demo`


#### make自动编译

编写CMakeLists.txt文件放在项目根目录

```shell
cmake_minimum_required(VERSION 3.14)
project(llama_demo)
# 设置 C++ 标准
# set(CMAKE_CXX_STANDARD 11)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 设置构建类型（Debug/Release）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
# 添加头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)
# 设置 lib 目录路径
set(LIB_DIR ${PROJECT_SOURCE_DIR}/lib)
# 查找 lib 目录下的所有 .so 文件（Linux）
file(GLOB LIBS "${LIB_DIR}/*.so")
if(LIBS)
    message(STATUS "Found shared libraries: ${LIBS}")
else()
    message(FATAL_ERROR "No .so files found in ${LIB_DIR}, please make sure they exist.")
endif()
# 添加可执行文件
add_executable(${PROJECT_NAME} src/main.cpp)
# 链接所有 .so 文件
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${LIBS}
        pthread
        m
)
# 设置运行时库路径（RPATH），让程序运行时能找到 .so 文件
set(CMAKE_INSTALL_RPATH "${CMAKE_SOURCE_DIR}/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
```

1. mkdir build
2. cd build
3. cmake ..         // 生成makefile等配置文件
4. cmake --build .  // 生成可执行文件



## 部署单片机

llama.cpp 强大之处在于自己用C++完全实现了模型的整个架构和推理代码，非常高效，而且支持量化使得垃圾cpu都能跑推理，比如单片机这样的性能羸弱的平台

也可以部署在移动端APP上，通过APP的方式使用离线模型，这种方式就得语言切换了，毕竟不能直接用C++开发APP。比如安卓平台就得通过JNI调用C/C++的方式使用llamacpp。也有现成的框架，llama.rn。通过react native开发。

如果部署单片机就简单多了，单片机的大模型和人交互，可以采用多种方式，蓝牙，或者跑一个简单的http服务器。

采用http服务器最方便，llama.cpp/examples内有chat等Demo代码，稍微改一改增加一个http功能，运行在单片机上即可。

带来的好处一目了然：

+ 减少语言切换成本（不需要用 Python、Node.js）
+ 降低系统资源占用（适合嵌入式、单片机、小型设备）
+ 保持部署简单（不需要运行时、虚拟机、容器）

这里选择cpp-httplib，非常简单和轻量，访问官网进行下载配置，也可以使用llama-cpp-python，基于python实现服务器

项目架构类似上个Demo

```c
#include "httplib.h"
#include "llama.h"  // 集成 llama.cpp 的头文件
// 已经加载了模型
struct llama_model* model;
struct llama_context* ctx;

// 生成文本的函数（简化）
std::string generate_response(const std::string& prompt) {
    // 设置 prompt
    llama_token bos = llama_token_bos(model);
    llama_token eos = llama_token_eos(model);
    // 编码 prompt
    std::vector<llama_token> tokens = encode_prompt(prompt);
    // 清除上下文
    llama_reset_context(ctx, nullptr);
    // 生成文本
    std::string output;
    for (int i = 0; i < 100; ++i) {  // 最多生成 100 个 token
        llama_token token = llama_sample_top_p(ctx, tokens.data(), tokens.size(), 0.95);
        tokens.push_back(token);
        output += llama_token_to_str(model, token);
        if (token == eos) break;
    }
    return output;
}
int main() {
    httplib::Server svr;
    // POST /generate
    svr.Post("/generate", [](const httplib::Request& req, httplib::Response& res) {
        std::string prompt = req.get_param_value("prompt");
        std::string response = generate_response(prompt);
        res.set_content(response, "text/plain");
    });
    // GET /health
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("OK", "text/plain");
    });
    printf("Server started at http://0.0.0.0:8080\n");
    svr.listen("0.0.0.0", 8080);
    return 0;
}
```

使用，连接单片机热点 或者单片机cmd内`curl "http://localhost:8080/generate?prompt=Hello"`

更好的交互方式：语音+蓝牙交互，以后研究

## 开启多模态支持

### 模型支持

llama.cpp项目初期就支持了llava，(lava.cpp,cli.cpp)，编译的llava-cli就用来跑llava

随后增加了mobileVlm支持（轻量llava）

随后又支持了若干模型，模型越多，会话的chat模版越复杂，添加了若干 xxx-cli导致异常复杂

最后引入了libmtmd替换lava.cpp，提供统一的命令行接口处理多模态输入，mtmd-cli就可以加载若干不同模型

### mmproj

> 全称 Multi-model project 

llamacpp支持的多模态实现，首先需要一个embedding层对图片进行编码输出向量。这个embedding层是独立于llamacpp项目的，现在的许多视觉模型（比如clip）都是基于ViT的，他们的预处理（归一，卷积等）和投影（图像特征->模型语义空间）差别很大，将它们直接集成到 libllama 中目前还比较困难，所以独立了embedding模型

因此llamacpp的多模态需要两个模型（中期融合）

+ llm
+ 处理图像的模型（encoding和projection两个工作）

> 获取和使用

这块还是看官网，提供了多种多样的模型和地址

如果选择Gemma 3 vision就执行`llama-mtmd-cli -hf ggml-org/gemma-3-4b-it-GGUF`,注意1b版本不支持视觉，支持音频。4b及以上才支持视觉。

cli下载的默认地址是`~/.cache/llama.cpp/ggml-org_gemma-3-4b-it-GGUF_gemma-3-4b-it-Q4_K_M.gguf`

### 多模态使用

正常指定基础模型和ViT结构的mmproj模型，手动加载本地模型使用：`llama-mtmd-cli -m {llm基础模型比如qwen2.5-1.5b-q4_0}.gguf --mmproj {mmproj模型}.gguf --image 图片.jpg`

Qwen2.5-VL 在ggml官方的库里下载最好，官方已经做好了转换：`https://github.com/ggml-org/llama.cpp/blob/master/docs/multimodal.md`

注意：最好不要手动下载模型，而是 `llama-server -hf ggml/Qwen2.5... ` 会自动下载模型和mmproj，手动下载不要只下载模型，还要点开`files and versions`找到里面对应的mmproj模型下载。

只要有模型+mmproj就可以进行多模态任务，模型可以是原版的，也可是ggml库的，但如果模型本身不支持多模态，强行加载会出错


## llama.cpp 的底层实现原理

llama.cpp 不依赖 PyTorch 或 TensorFlow，而是自己实现了：
+ LLaMA 模型的结构（如 attention、feed-forward、RMSNorm 等）
+ 权重加载与解析（从 HuggingFace 或 Meta 的原始格式）
+ 自回归生成（逐 token 生成）

### 权重加载与内存管理

+ llama.cpp 将模型权重加载为 二进制格式（.bin）
+ 每个权重张量都被映射为一个结构体（ggml_tensor）
+ 使用自定义内存管理器（ggml）来处理 tensor 的分配和释放

### 自定义张量计算库：ggml
ggml 是 llama.cpp 的底层计算库（类似于 PyTorch 的 autograd）
特点：
+ 支持 CPU 指令集加速（如 AVX、AVX2、FMA、NEON）
+ 支持 GPU 加速（Metal、CUDA、Vulkan）
+ 支持自动微分（但主要用于推理）
+ 支持 量化计算（Q4_0、Q4_1、Q5_0、Q8_0 等）

###  量化

llama.cpp 最大的亮点之一是 支持模型量化，将浮点数（FP32）转换为低精度整数（如 4-bit、5-bit、8-bit）

量化后模型体积大幅减小，推理速度提升，内存占用降低。 例如：原始 LLaMA-7B 模型：约 13GB，量化为 Q4_0 后：约 3.5GB，甚至更低

### 推理引擎
llama.cpp 实现了一个轻量的推理引擎：

+ 支持自回归生成（逐 token 生成）
+ 支持上下文缓存（KV Cache）优化
+ 支持多种采样策略（如 greedy、top-k、top-p、temperature）
+ 所有推理逻辑都用 纯 C/C++ 实现，不依赖 Python


### 源码阅读例子

关键函数，计算矩阵相乘，可以看看llamacpp底层是怎么实现的，源码位置（ggml/src/ggml.c 2904行）：
```c
/**
 * ggml_mul_mat - 构造一个矩阵乘法操作的张量节点
 *
 * 该函数不会立即执行矩阵乘法运算，而是创建一个张量节点，
 * 表示两个输入张量 a 和 b 的矩阵乘法操作。
 * 真正的计算会在 ggml_graph_compute 阶段执行。
 *
 * 参数：
 *   ctx - ggml 上下文，用于内存管理
 *   a   - 第一个输入张量，形状应为 [K, M]
 *   b   - 第二个输入张量，形状应为 [K, N]
 *
 * 返回：
 *   一个新的张量，表示 a × b 的结果，形状为 [M, N]
 */
struct ggml_tensor * ggml_mul_mat(
        struct ggml_context * ctx,
        struct ggml_tensor  * a,
        struct ggml_tensor  * b) {

    // 确保两个张量可以进行矩阵乘法：
    // a 的列数必须等于 b 的列数（即 a->ne == K，b->ne == K）
    GGML_ASSERT(ggml_can_mul_mat(a, b));

    // ggml 不支持对 a 进行转置，所以必须确保 a 没有被转置
    GGML_ASSERT(!ggml_is_transposed(a));

    // 构造输出张量的维度：
    // a->ne 是 a 的列数 M
    // b->ne 是 b 的列数 N
    // 输出张量的维度为 [M, N, 1, 1]，即形状为 M x N
    const int64_t ne = { a->ne b->ne b->ne b->ne };

    // 创建一个新的张量，类型为 float32（F32），维度为 4（虽然只用前两个）
    struct ggml_tensor * result = ggml_new_tensor(ctx, GGML_TYPE_F32, 4, ne);

    // 设置该张量的操作类型为矩阵乘法（GGML_OP_MUL_MAT）
    result->op = GGML_OP_MUL_MAT;

    // 设置该张量的两个输入源张量
    result->src = a; // 第一个输入是 a
    result->src = b; // 第二个输入是 b

    // 返回构造好的张量节点
    return result;
}
```


