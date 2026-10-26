#include <jni.h>
#include <string>
#include <vector>
#include <cstdio>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>
#include "stable-diffusion.h"

#define TAG "TITAN_ENGINE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static int pipe_stdout[2];
static pthread_t log_thread;
static std::string g_log_buffer;

void* log_capture_func(void*) {
    ssize_t bytes_read;
    char buf[1024];
    while ((bytes_read = read(pipe_stdout[0], buf, sizeof(buf) - 1)) > 0) {
        buf[bytes_read] = '\0';
        g_log_buffer += buf;
        if (g_log_buffer.size() > 65536) {
            g_log_buffer.erase(0, g_log_buffer.size() - 65536);
        }
    }
    return nullptr;
}

extern "C" JNIEXPORT void JNICALL
Java_com_titan_sd_EngineService_initLogging(JNIEnv* env, jobject thiz) {
    pipe(pipe_stdout);
    dup2(pipe_stdout[1], STDOUT_FILENO);
    dup2(pipe_stdout[1], STDERR_FILENO);
    pthread_create(&log_thread, nullptr, log_capture_func, nullptr);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_titan_sd_EngineService_getEngineLogs(JNIEnv* env, jobject thiz) {
    return env->NewStringUTF(g_log_buffer.c_str());
}

static void save_bmp(const char* filename, uint32_t width, uint32_t height, uint32_t channels, const uint8_t* data) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    uint32_t row_padded = (width * 3 + 3) & (~3);
    uint32_t file_size = 54 + row_padded * height;
    unsigned char header[54] = {
        'B','M',
        (unsigned char)(file_size), (unsigned char)(file_size>>8), (unsigned char)(file_size>>16), (unsigned char)(file_size>>24),
        0, 0, 0, 0, 54, 0, 0, 0,
        40, 0, 0, 0,
        (unsigned char)(width), (unsigned char)(width>>8), (unsigned char)(width>>16), (unsigned char)(width>>24),
        (unsigned char)(height), (unsigned char)(height>>8), (unsigned char)(height>>16), (unsigned char)(height>>24),
        1, 0, 24, 0, 0, 0, 0, 0,
        (unsigned char)(row_padded * height), (unsigned char)((row_padded * height)>>8), (unsigned char)((row_padded * height)>>16), (unsigned char)((row_padded * height)>>24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    fwrite(header, 1, 54, f);
    std::vector<uint8_t> row(row_padded, 0);
    for (int y = (int)height - 1; y >= 0; y--) {
        for (int x = 0; x < (int)width; x++) {
            int src_idx = (y * width + x) * channels;
            row[x * 3 + 0] = data[src_idx + 2];
            row[x * 3 + 1] = data[src_idx + 1];
            row[x * 3 + 2] = data[src_idx + 0];
        }
        fwrite(row.data(), 1, row_padded, f);
    }
    fclose(f);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_titan_sd_EngineService_runGeneration(
    JNIEnv* env, jobject thiz,
    jstring j_dit_path, jstring j_vae_path, jstring j_llm_path,
    jobjectArray j_loras, jfloatArray j_lora_weights,
    jstring j_prompt, jstring j_neg_prompt, jstring j_init_img,
    jint width, jint height, jint steps, jfloat cfg,
    jboolean tiled_vae, jint tile_w, jint tile_h, jint tile_overlap,
    jboolean use_gpu, jstring j_out_path
) {
    const char* dit = env->GetStringUTFChars(j_dit_path, nullptr);
    const char* vae = env->GetStringUTFChars(j_vae_path, nullptr);
    const char* llm = env->GetStringUTFChars(j_llm_path, nullptr);
    const char* prompt = env->GetStringUTFChars(j_prompt, nullptr);
    const char* neg_prompt = env->GetStringUTFChars(j_neg_prompt, nullptr);
    const char* out_path = env->GetStringUTFChars(j_out_path, nullptr);

    sd_ctx_params_t ctx_params;
    sd_ctx_params_init(&ctx_params);
    ctx_params.diffusion_model_path = dit;
    ctx_params.vae_path = vae;
    ctx_params.llm_path = llm;
    ctx_params.n_threads = 4;
    ctx_params.backend = use_gpu ? "vulkan0" : "cpu";

    sd_ctx_t* ctx = new_sd_ctx(&ctx_params);
    if (!ctx) {
        LOGE("Failed to create sd context");
        env->ReleaseStringUTFChars(j_dit_path, dit);
        env->ReleaseStringUTFChars(j_vae_path, vae);
        env->ReleaseStringUTFChars(j_llm_path, llm);
        env->ReleaseStringUTFChars(j_prompt, prompt);
        env->ReleaseStringUTFChars(j_neg_prompt, neg_prompt);
        env->ReleaseStringUTFChars(j_out_path, out_path);
        return JNI_FALSE;
    }

    sd_img_gen_params_t gen_params;
    sd_img_gen_params_init(&gen_params);
    gen_params.prompt = prompt;
    gen_params.negative_prompt = neg_prompt;
    gen_params.width = width;
    gen_params.height = height;
    gen_params.sample_params.sample_steps = steps;
    gen_params.sample_params.guidance.txt_cfg = cfg;
    gen_params.sample_params.sample_method = EULER_SAMPLE_METHOD;
    gen_params.seed = -1;

    sd_image_generation_result_t* result = nullptr;
    bool success = false;

    if (generate_image(ctx, &gen_params, &result)) {
        if (result && result->items && result->count > 0 && result->items[0].image.data) {
            save_bmp(out_path, result->items[0].image.width, result->items[0].image.height, result->items[0].image.channel, result->items[0].image.data);
            success = true;
        }
    }

    if (result) {
        free_sd_image_generation_result(result);
    }

    free_sd_ctx(ctx);

    env->ReleaseStringUTFChars(j_dit_path, dit);
    env->ReleaseStringUTFChars(j_vae_path, vae);
    env->ReleaseStringUTFChars(j_llm_path, llm);
    env->ReleaseStringUTFChars(j_prompt, prompt);
    env->ReleaseStringUTFChars(j_neg_prompt, neg_prompt);
    env->ReleaseStringUTFChars(j_out_path, out_path);

    return success ? JNI_TRUE : JNI_FALSE;
}
