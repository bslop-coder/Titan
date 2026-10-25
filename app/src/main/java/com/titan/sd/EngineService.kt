package com.titan.sd

import android.app.Service
import android.content.Intent
import android.os.Binder
import android.os.IBinder

class EngineService : Service() {
    private val binder = LocalBinder()

    companion object {
        init {
            System.loadLibrary("titan_jni")
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
        initLogging()
    }

    override fun onBind(intent: Intent?): IBinder = binder
}
