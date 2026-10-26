package com.titan.sd

import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Collections
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.unit.dp
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import kotlin.concurrent.thread

class MainActivity : ComponentActivity() {

    private fun saveCrashLog(t: Throwable) {
        val sw = StringWriter()
        t.printStackTrace(PrintWriter(sw))
        try {
            File(filesDir, "startup_crash.txt").writeText(sw.toString())
        } catch (_: Exception) {}
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        Thread.setDefaultUncaughtExceptionHandler { _, throwable ->
            saveCrashLog(throwable)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).apply {
                        data = Uri.parse("package:$packageName")
                    }
                    startActivity(intent)
                } catch (_: Exception) {
                    val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    startActivity(intent)
                }
            }
        }

        setContent {
            MaterialTheme(colorScheme = darkColorScheme(primary = Color(0xFFFF9800))) {
                TitanMainScreen(this)
            }
        }
    }
}

fun resolveFilePath(uri: Uri): String {
    try {
        if (DocumentsContract.isDocumentUri(null, uri) || uri.authority == "com.android.externalstorage.documents") {
            val docId = DocumentsContract.getDocumentId(uri)
            val split = docId.split(":")
            if (split.size >= 2) {
                val type = split[0]
                if ("primary".equals(type, ignoreCase = true)) {
                    return "/sdcard/${split[1]}"
                } else {
                    return "/storage/$type/${split[1]}"
                }
            }
        }
    } catch (_: Exception) {}
    return uri.path?.replace("/document/primary:", "/sdcard/") ?: ""
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TitanMainScreen(context: Context) {
    var showSettings by remember { mutableStateOf(false) }
    var showCrashLog by remember { mutableStateOf(false) }
    var isGenerating by remember { mutableStateOf(false) }
    var prompt by remember { mutableStateOf("") }
    var negPrompt by remember { mutableStateOf("") }
    var generatedImagePath by remember { mutableStateOf<String?>(null) }
    var crashLogContent by remember { mutableStateOf("") }

    val startupCrashFile = remember { File(context.filesDir, "startup_crash.txt") }
    LaunchedEffect(Unit) {
        if (startupCrashFile.exists()) {
            crashLogContent = startupCrashFile.readText()
            showCrashLog = true
            startupCrashFile.delete()
        } else if (EngineService.libraryError != null) {
            crashLogContent = "Native library load error: ${EngineService.libraryError}"
            showCrashLog = true
        }
    }

    var ditPath by remember { mutableStateOf("/sdcard/SDA/anima-turbo-v1.1-Q4_0.gguf") }
    var vaePath by remember { mutableStateOf("/sdcard/SDA/qwen_image_vae.safetensors") }
    var llmPath by remember { mutableStateOf("/sdcard/SDA/Qwen3-0.6B-Base.Q8_0.gguf") }
    var backendMode by remember { mutableIntStateOf(1) }
    var tiledVae by remember { mutableStateOf(true) }
    var width by remember { mutableStateOf(768f) }
    var height by remember { mutableStateOf(768f) }
    var steps by remember { mutableStateOf(8f) }
    var cfg by remember { mutableStateOf(1.0f) }

    val loraSlots = remember { mutableStateListOf("", "", "", "") }
    val loraStrengths = remember { mutableStateListOf(1.0f, 1.0f, 1.0f, 1.0f) }

    val ditPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { ditPath = resolveFilePath(it) }
    }
    val vaePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { vaePath = resolveFilePath(it) }
    }
    val llmPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let { llmPath = resolveFilePath(it) }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Titan") },
                actions = {
                    IconButton(onClick = { }) {
                        Icon(Icons.Filled.Add, contentDescription = "Add Image")
                    }
                    IconButton(onClick = { }) {
                        Icon(Icons.Filled.Collections, contentDescription = "Gallery")
                    }
                    IconButton(onClick = { showSettings = true }) {
                        Icon(Icons.Filled.Settings, contentDescription = "Settings")
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            OutlinedTextField(
                value = prompt,
                onValueChange = { prompt = it },
                label = { Text("Positive Prompt") },
                modifier = Modifier.fillMaxWidth(),
                maxLines = 3
            )

            OutlinedTextField(
                value = negPrompt,
                onValueChange = { negPrompt = it },
                label = { Text("Negative Prompt") },
                modifier = Modifier.fillMaxWidth(),
                maxLines = 2
            )

            Button(
                onClick = {
                    if (isGenerating) return@Button

                    if (!File(ditPath).exists()) {
                        crashLogContent = "DiT Model file not found at:\n$ditPath\n\nPlease check path in Settings or tap 📁 Browse."
                        showCrashLog = true
                        return@Button
                    }
                    if (!File(vaePath).exists()) {
                        crashLogContent = "VAE file not found at:\n$vaePath\n\nPlease check path in Settings or tap 📁 Browse."
                        showCrashLog = true
                        return@Button
                    }
                    if (!File(llmPath).exists()) {
                        crashLogContent = "Text Encoder file not found at:\n$llmPath\n\nPlease check path in Settings or tap 📁 Browse."
                        showCrashLog = true
                        return@Button
                    }

                    isGenerating = true
                    val outDir = File("/sdcard/Pictures/Titan")
                    if (!outDir.exists()) outDir.mkdirs()
                    val outPath = "/sdcard/Pictures/Titan/Titan_${System.currentTimeMillis()}.bmp"

                    thread {
                        try {
                            val success = EngineService.runGeneration(
                                ditPath, vaePath, llmPath,
                                loraSlots.toTypedArray(), loraStrengths.toFloatArray(),
                                prompt, negPrompt, "",
                                width.toInt(), height.toInt(), steps.toInt(), cfg,
                                tiledVae, 512, 512, 128,
                                backendMode, outPath
                            )

                            if (success) {
                                generatedImagePath = outPath
                            } else {
                                val logs = EngineService.getEngineLogs().trim()
                                crashLogContent = if (logs.isNotEmpty()) logs else "Engine initialization returned null."
                                showCrashLog = true
                            }
                        } catch (e: Exception) {
                            crashLogContent = e.toString()
                            showCrashLog = true
                        } finally {
                            isGenerating = false
                        }
                    }
                },
                modifier = Modifier.fillMaxWidth(),
                enabled = !isGenerating
            ) {
                Text(if (isGenerating) "Generating..." else "Generate")
            }

            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .background(Color(0xFF1E1E1E)),
                contentAlignment = Alignment.Center
            ) {
                if (isGenerating) {
                    CircularProgressIndicator(color = Color(0xFFFF9800))
                } else {
                    generatedImagePath?.let { path ->
                        val bitmap = BitmapFactory.decodeFile(path)
                        if (bitmap != null) {
                            Image(
                                bitmap = bitmap.asImageBitmap(),
                                contentDescription = "Output Image",
                                modifier = Modifier.fillMaxSize()
                            )
                        }
                    } ?: Text("Generated image will appear here", color = Color.Gray)
                }
            }
        }
    }

    if (showSettings) {
        ModalBottomSheet(onDismissRequest = { showSettings = false }) {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(20.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text("Titan Settings", style = MaterialTheme.typography.titleLarge)

                Text("Compute Engine", style = MaterialTheme.typography.titleMedium)
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = backendMode == 1, onClick = { backendMode = 1 })
                        Text("Vulkan Hybrid (GPU DiT + CPU Text/VAE) - Recommended")
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = backendMode == 0, onClick = { backendMode = 0 })
                        Text("CPU Only (ARM NEON Dot-Product)")
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = backendMode == 2, onClick = { backendMode = 2 })
                        Text("Vulkan Full (All on GPU)")
                    }
                }

                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Tiled VAE (512x512, 128 overlap)")
                    Spacer(Modifier.weight(1f))
                    Switch(checked = tiledVae, onCheckedChange = { tiledVae = it })
                }

                Text("Width: ${width.toInt()} px")
                Slider(value = width, onValueChange = { width = it }, valueRange = 256f..2048f, steps = 27)

                Text("Height: ${height.toInt()} px")
                Slider(value = height, onValueChange = { height = it }, valueRange = 256f..2048f, steps = 27)

                Text("Steps: ${steps.toInt()}")
                Slider(value = steps, onValueChange = { steps = it }, valueRange = 1f..60f)

                Text("CFG Scale: $cfg")
                Slider(value = cfg, onValueChange = { cfg = it }, valueRange = 1.0f..15.0f)

                Text("Model Paths", style = MaterialTheme.typography.titleMedium)

                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(
                        value = ditPath,
                        onValueChange = { ditPath = it },
                        label = { Text("DiT Model Path") },
                        modifier = Modifier.weight(1f)
                    )
                    IconButton(onClick = { ditPicker.launch(arrayOf("*/*")) }) {
                        Icon(Icons.Filled.FolderOpen, contentDescription = "Browse DiT")
                    }
                }

                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(
                        value = vaePath,
                        onValueChange = { vaePath = it },
                        label = { Text("VAE Path") },
                        modifier = Modifier.weight(1f)
                    )
                    IconButton(onClick = { vaePicker.launch(arrayOf("*/*")) }) {
                        Icon(Icons.Filled.FolderOpen, contentDescription = "Browse VAE")
                    }
                }

                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(
                        value = llmPath,
                        onValueChange = { llmPath = it },
                        label = { Text("Text Encoder Path") },
                        modifier = Modifier.weight(1f)
                    )
                    IconButton(onClick = { llmPicker.launch(arrayOf("*/*")) }) {
                        Icon(Icons.Filled.FolderOpen, contentDescription = "Browse LLM")
                    }
                }

                Text("LoRA Slots", style = MaterialTheme.typography.titleMedium)
                for (i in 0 until 4) {
                    OutlinedTextField(
                        value = loraSlots[i],
                        onValueChange = { loraSlots[i] = it },
                        label = { Text("LoRA ${i + 1} Path") },
                        modifier = Modifier.fillMaxWidth()
                    )
                    Slider(value = loraStrengths[i], onValueChange = { loraStrengths[i] = it }, valueRange = 0.0f..2.0f)
                }
            }
        }
    }

    if (showCrashLog) {
        AlertDialog(
            onDismissRequest = { showCrashLog = false },
            title = { Text("Diagnostics & Engine Details") },
            text = {
                Text(crashLogContent, modifier = Modifier.verticalScroll(rememberScrollState()))
            },
            confirmButton = {
                Button(onClick = { showCrashLog = false }) { Text("Dismiss") }
            }
        )
    }
}
