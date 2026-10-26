package com.titan.sd

import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder

class EngineService : Service() {
    private val binder = LocalBinder()

    companion object {
        var libraryError: String? = null
        init {
            try {
                System.loadLibrary("titan_jni")
            } catch (t: Throwable) {
                libraryError = t.toString()
            }
        }
    }

    external fun initLogging()
    external fun getEngineLogs(): String
    external fun runGeneration(
        ditPath: String, vaePath: String, llmPath: String,
        loras: Array<String>, loraWeights: FloatArray,
        prompt: String, negPrompt: String, initImg: String,
        width: Int, height: Int, steps: Int, cfg: Float,
        tiledVae: Boolean, tileW: Int, tileH: Int, tileOverlap: Int,
        useGpu: Boolean, outPath: String
    ): Boolean

    inner class LocalBinder : Binder() {
        fun getService(): EngineService = this@EngineService
    }

    override fun onCreate() {
        super.onCreate()
        if (libraryError == null) {
            try {
                initLogging()
            } catch (_: Throwable) {}
        }
    }

    override fun onBind(intent: Intent?): IBinder = binder
}
