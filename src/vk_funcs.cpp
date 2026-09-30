#include "vk_funcs.h"
#include <cstdio>

#define MB_VK_DEFINE(name) PFN_##name name = nullptr;
VK_GLOBAL_FUNCS(MB_VK_DEFINE)
VK_INSTANCE_FUNCS(MB_VK_DEFINE)
VK_DEVICE_FUNCS(MB_VK_DEFINE)
#undef MB_VK_DEFINE

static PFN_vkGetInstanceProcAddr g_gipa = nullptr;

bool vkLoadGlobal(PFN_vkGetInstanceProcAddr gipa) {
    g_gipa = gipa;
    bool ok = true;
#define LOAD(name)                                                    \
    name = (PFN_##name)g_gipa(VK_NULL_HANDLE, #name);                 \
    if (!name) { std::fprintf(stderr, "Missing Vulkan function %s\n", #name); ok = false; }
    VK_GLOBAL_FUNCS(LOAD)
#undef LOAD
    return ok;
}

bool vkLoadInstance(VkInstance instance) {
    bool ok = true;
#define LOAD(name)                                                    \
    name = (PFN_##name)g_gipa(instance, #name);                       \
    if (!name) { std::fprintf(stderr, "Missing Vulkan function %s\n", #name); ok = false; }
    VK_INSTANCE_FUNCS(LOAD)
#undef LOAD
    return ok;
}

bool vkLoadDevice(VkDevice device) {
    bool ok = true;
#define LOAD(name)                                                    \
    name = (PFN_##name)vkGetDeviceProcAddr(device, #name);            \
    if (!name) { std::fprintf(stderr, "Missing Vulkan function %s\n", #name); ok = false; }
    VK_DEVICE_FUNCS(LOAD)
#undef LOAD
    return ok;
}

PFN_vkVoidFunction vkLoadInstanceFunc(VkInstance instance, const char* name) {
    return g_gipa ? g_gipa(instance, name) : nullptr;
}
