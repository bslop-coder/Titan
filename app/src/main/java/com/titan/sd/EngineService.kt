package com.titan.sd

object EngineService {
    var libraryError: String? = null

    init {
        try {
            System.loadLibrary("titan_jni")
            initLogging()
        } catch (t: Throwable) {
            libraryError = t.toString()
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
        backendMode: Int, outPath: String
    ): Boolean
}
