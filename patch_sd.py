import os

vk_dir = "app/src/main/cpp/sdcpp/ggml/src/ggml-vulkan"

path_buf = os.path.join(vk_dir, "ggml-vulkan-buffers.cpp")
if os.path.exists(path_buf):
    with open(path_buf, "r") as f:
        t = f.read()
    t = t.replace("return std::aligned_alloc(align, size);", "void* ptr = nullptr; if (posix_memalign(&ptr, align, size) == 0) return ptr; return nullptr;")
    t = t.replace("if (device->buffer_device_address)", "if (false)")
    t = t.replace("if (device.buffer_device_address)", "if (false)")
    with open(path_buf, "w") as f:
        f.write(t)
    print("Patched ggml-vulkan-buffers.cpp")

path_vk = os.path.join(vk_dir, "ggml-vulkan.cpp")
if os.path.exists(path_vk):
    with open(path_vk, "r") as f:
        t_vk = f.read()

    t_vk = t_vk.replace("device->buffer_device_address = true;", "device->buffer_device_address = false;")
    t_vk = t_vk.replace("buffer_device_address = true", "buffer_device_address = false")

    target_vk = "h->queue = device->device.getQueue2(queue_info2);"
    patch_vk = """h->queue = device->device.getQueue2(queue_info2);
    if (!h->queue) {
        h->queue = device->device.getQueue(queue_info2.queueFamilyIndex, queue_info2.queueIndex);
    }"""
    if target_vk in t_vk:
        t_vk = t_vk.replace(target_vk, patch_vk)

    with open(path_vk, "w") as f:
        f.write(t_vk)
    print("Patched ggml-vulkan.cpp")
