#include <jni.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>
#include "stable-diffusion.h"

#define TAG "TITAN_ENGINE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

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

    sd_context_params_t params = sd_context_params_default();
    params.vae_tiling = tiled_vae;
    params.free_params_immediately = true;

    sd_ctx_t* ctx = new_sd_ctx(
        dit, vae, "", llm, "", "", "", "",
        false, false, params.vae_tiling, 4,
        SD_TYPE_COUNT, RNG_TYPE_DEFAULT, DEFAULT,
        false, true, true
    );

    if (!ctx) {
        return JNI_FALSE;
    }

    free_sd_ctx(ctx);
    env->ReleaseStringUTFChars(j_dit_path, dit);
    env->ReleaseStringUTFChars(j_vae_path, vae);
    env->ReleaseStringUTFChars(j_llm_path, llm);
    return JNI_TRUE;
}
