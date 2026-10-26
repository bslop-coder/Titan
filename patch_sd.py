import os

path_buf = "app/src/main/cpp/sdcpp/ggml/src/ggml-vulkan/ggml-vulkan-buffers.cpp"
if os.path.exists(path_buf):
    with open(path_buf, "r") as f:
        t = f.read()
    target_buf = "return std::aligned_alloc(align, size);"
    patch_buf = "void* ptr = nullptr; if (posix_memalign(&ptr, align, size) == 0) return ptr; return nullptr;"
    if target_buf in t:
        t = t.replace(target_buf, patch_buf)
        with open(path_buf, "w") as f:
            f.write(t)
        print("Patched aligned_alloc with posix_memalign")

path_vk = "app/src/main/cpp/sdcpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp"
if os.path.exists(path_vk):
    with open(path_vk, "r") as f:
        t_vk = f.read()
    target_vk = "h->queue = device->device.getQueue2(queue_info2);"
    patch_vk = """h->queue = device->device.getQueue2(queue_info2);
    if (!h->queue) {
        h->queue = device->device.getQueue(queue_info2.queueFamilyIndex, queue_info2.queueIndex);
    }"""
    if target_vk in t_vk:
        t_vk = t_vk.replace(target_vk, patch_vk)
        with open(path_vk, "w") as f:
            f.write(t_vk)
        print("Patched Mali vkGetDeviceQueue2 fallback")
