#include <jni.h>
#include <string>
#include <vector>
#include <cstdio>
#include <unistd.h>
#include <pthread.h>
#include <chrono>
#include <exception>
#include <android/log.h>
#include "stable-diffusion.h"

#define TAG "TITAN_NATIVE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static std::string g_log_buffer;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static sd_ctx_t* g_sd_ctx = nullptr;
static std::string g_cached_dit;
static std::string g_cached_vae;
static std::string g_cached_llm;
static int g_cached_backend_mode = -999;

static void titan_log_callback(sd_log_level_t level, const char* text, void* data) {
    if (text) {
        pthread_mutex_lock(&g_log_mutex);
        g_log_buffer += text;
        if (g_log_buffer.size() > 65536) {
            g_log_buffer.erase(0, g_log_buffer.size() - 65536);
        }
        pthread_mutex_unlock(&g_log_mutex);
        __android_log_print(ANDROID_LOG_INFO, TAG, "%s", text);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_titan_sd_EngineService_initLogging(JNIEnv* env, jobject thiz) {
    sd_set_log_callback(titan_log_callback, nullptr);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_titan_sd_EngineService_getEngineLogs(JNIEnv* env, jobject thiz) {
    pthread_mutex_lock(&g_log_mutex);
    jstring res = env->NewStringUTF(g_log_buffer.c_str());
    pthread_mutex_unlock(&g_log_mutex);
    return res;
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
    jint backend_mode, jstring j_out_path
) {
    const char* dit = env->GetStringUTFChars(j_dit_path, nullptr);
    const char* vae = env->GetStringUTFChars(j_vae_path, nullptr);
    const char* llm = env->GetStringUTFChars(j_llm_path, nullptr);
    const char* prompt = env->GetStringUTFChars(j_prompt, nullptr);
    const char* neg_prompt = env->GetStringUTFChars(j_neg_prompt, nullptr);
    const char* out_path = env->GetStringUTFChars(j_out_path, nullptr);

    pthread_mutex_lock(&g_log_mutex);
    g_log_buffer.clear();
    pthread_mutex_unlock(&g_log_mutex);

    sd_set_log_callback(titan_log_callback, nullptr);

    bool success = false;

    try {
        bool needs_reload = (g_sd_ctx == nullptr) ||
                            (g_cached_dit != dit) ||
                            (g_cached_vae != vae) ||
                            (g_cached_llm != llm) ||
                            (g_cached_backend_mode != backend_mode);

        if (needs_reload) {
            if (g_sd_ctx != nullptr) {
                free_sd_ctx(g_sd_ctx);
                g_sd_ctx = nullptr;
            }

            auto t_load_start = std::chrono::high_resolution_clock::now();

            sd_ctx_params_t ctx_params;
            sd_ctx_params_init(&ctx_params);
            ctx_params.diffusion_model_path = dit;
            ctx_params.vae_path = vae;
            ctx_params.llm_path = llm;
            ctx_params.n_threads = 6;
            ctx_params.diffusion_flash_attn = false;
            ctx_params.flash_attn_mode = "off";

            if (backend_mode == 0) {
                ctx_params.backend = "cpu";
                ctx_params.params_backend = "cpu";
            } else if (backend_mode == 1) {
                ctx_params.backend = "diffusion=vulkan0,te=cpu,vae=cpu";
                ctx_params.params_backend = "diffusion=vulkan0,te=cpu,vae=cpu";
            } else {
                ctx_params.backend = "vulkan0";
                ctx_params.params_backend = "vulkan0";
            }

            g_sd_ctx = new_sd_ctx(&ctx_params);

            auto t_load_end = std::chrono::high_resolution_clock::now();
            double load_duration = std::chrono::duration<double>(t_load_end - t_load_start).count();

            if (!g_sd_ctx) {
                LOGE("Failed to create sd context");
                pthread_mutex_lock(&g_log_mutex);
                g_log_buffer += "\nFailed to initialize model context.";
                pthread_mutex_unlock(&g_log_mutex);
            } else {
                g_cached_dit = dit;
                g_cached_vae = vae;
                g_cached_llm = llm;
                g_cached_backend_mode = backend_mode;
                pthread_mutex_lock(&g_log_mutex);
                g_log_buffer += "\n[Model Loaded into memory in " + std::to_string(load_duration) + "s]\n";
                pthread_mutex_unlock(&g_log_mutex);
            }
        } else {
            pthread_mutex_lock(&g_log_mutex);
            g_log_buffer += "\n[Using already-loaded model from memory]\n";
            pthread_mutex_unlock(&g_log_mutex);
        }

        if (g_sd_ctx != nullptr) {
            auto t_gen_start = std::chrono::high_resolution_clock::now();

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
            if (generate_image(g_sd_ctx, &gen_params, &result)) {
                if (result && result->items && result->count > 0 && result->items[0].image.data) {
                    save_bmp(out_path, result->items[0].image.width, result->items[0].image.height, result->items[0].image.channel, result->items[0].image.data);
                    success = true;
                }
            }

            auto t_gen_end = std::chrono::high_resolution_clock::now();
            double gen_duration = std::chrono::duration<double>(t_gen_end - t_gen_start).count();

            pthread_mutex_lock(&g_log_mutex);
            g_log_buffer += "\n[Inference Time: " + std::to_string(gen_duration) + "s (" + std::to_string(gen_duration / steps) + "s/step)]\n";
            pthread_mutex_unlock(&g_log_mutex);

            if (result) {
                free_sd_image_generation_result(result);
            }
        }
    } catch (const std::exception& e) {
        LOGE("C++ Exception: %s", e.what());
        pthread_mutex_lock(&g_log_mutex);
        g_log_buffer += std::string("\nHardware Exception: ") + e.what();
        pthread_mutex_unlock(&g_log_mutex);
        success = false;
    } catch (...) {
        LOGE("Unknown hardware exception caught");
        pthread_mutex_lock(&g_log_mutex);
        g_log_buffer += "\nUnknown hardware driver exception caught.";
        pthread_mutex_unlock(&g_log_mutex);
        success = false;
    }

    env->ReleaseStringUTFChars(j_dit_path, dit);
    env->ReleaseStringUTFChars(j_vae_path, vae);
    env->ReleaseStringUTFChars(j_llm_path, llm);
    env->ReleaseStringUTFChars(j_prompt, prompt);
    env->ReleaseStringUTFChars(j_neg_prompt, neg_prompt);
    env->ReleaseStringUTFChars(j_out_path, out_path);

    return success ? JNI_TRUE : JNI_FALSE;
}
