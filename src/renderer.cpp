// Vulkan renderer: cascaded shadow maps -> HDR scene with 4x MSAA (terrain, skinned
// creatures, instanced props, GPU grass, water, soft particles) -> bloom chain ->
// tonemapped / colour-graded composite + UI on the swapchain.
#include "renderer.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

#include "lit_vert.h"
#include "skin_vert.h"
#include "lit_frag.h"
#include "terrain_vert.h"
#include "terrain_frag.h"
#include "shadow_vert.h"
#include "shadow_skin_vert.h"
#include "grass_vert.h"
#include "grass_frag.h"
#include "water_vert.h"
#include "water_frag.h"
#include "sky_vert.h"
#include "sky_frag.h"
#include "particle_vert.h"
#include "particle_frag.h"
#include "post_vert.h"
#include "ssao_frag.h"
#include "rays_frag.h"
#include "shadow_frag.h"
#include "bloom_down_frag.h"
#include "bloom_up_frag.h"
#include "composite_frag.h"
#include "ui_vert.h"
#include "ui_frag.h"

#define VK_CHECK(x)                                                                          \
    do {                                                                                     \
        VkResult r_ = (x);                                                                   \
        if (r_ != VK_SUCCESS) {                                                              \
            std::fprintf(stderr, "Vulkan error %d at %s:%d\n", (int)r_, __FILE__, __LINE__); \
            return false;                                                                    \
        }                                                                                    \
    } while (0)

// ---------------------------------------------------------------------------
// Primitive meshes
// ---------------------------------------------------------------------------
// Builtin primitives: vertex alpha 1 so the instance alpha controls their glow.
static Vertex V(vec3 p, vec3 n) { return {p, n, vec4(1, 1, 1, 1), vec4(0, 0, 0, 1), 0}; }

static void addFace(std::vector<Vertex>& v, std::vector<uint32_t>& idx, vec3 a, vec3 b, vec3 c, vec3 d, vec3 n) {
    uint32_t base = (uint32_t)v.size();
    v.push_back(V(a, n));
    v.push_back(V(b, n));
    v.push_back(V(c, n));
    v.push_back(V(d, n));
    idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void buildCube(std::vector<Vertex>& v, std::vector<uint32_t>& i) {
    float h = 0.5f;
    addFace(v, i, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});
    addFace(v, i, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1});
    addFace(v, i, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1, 0, 0});
    addFace(v, i, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1, 0, 0});
    addFace(v, i, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0, 1, 0});
    addFace(v, i, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0, -1, 0});
}

void buildSphere(std::vector<Vertex>& v, std::vector<uint32_t>& idx, int rings, int segs) {
    uint32_t base = (uint32_t)v.size();
    for (int r = 0; r <= rings; r++) {
        float phi = PI * r / rings;
        for (int s = 0; s <= segs; s++) {
            float th = TAU * s / segs;
            vec3 n(std::sin(phi) * std::sin(th), std::cos(phi), std::sin(phi) * std::cos(th));
            v.push_back(V(n * 0.5f, n));
        }
    }
    for (int r = 0; r < rings; r++)
        for (int s = 0; s < segs; s++) {
            uint32_t a = base + r * (segs + 1) + s, b = a + segs + 1;
            idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
}

void buildCone(std::vector<Vertex>& v, std::vector<uint32_t>& idx, int segs) {
    float slope = std::atan2(0.5f, 1.0f);
    for (int s = 0; s < segs; s++) {
        float a0 = TAU * s / segs, a1 = TAU * (s + 1) / segs, am = (a0 + a1) * 0.5f;
        vec3 p0(std::sin(a0) * 0.5f, -0.5f, std::cos(a0) * 0.5f);
        vec3 p1(std::sin(a1) * 0.5f, -0.5f, std::cos(a1) * 0.5f);
        auto sn = [&](float a) { return normalize(vec3(std::sin(a) * std::cos(slope), std::sin(slope), std::cos(a) * std::cos(slope))); };
        uint32_t b = (uint32_t)v.size();
        v.push_back(V(p0, sn(a0)));
        v.push_back(V(p1, sn(a1)));
        v.push_back(V(vec3(0, 0.5f, 0), sn(am)));
        idx.insert(idx.end(), {b, b + 1, b + 2});
        uint32_t c = (uint32_t)v.size();
        v.push_back(V(p1, {0, -1, 0}));
        v.push_back(V(p0, {0, -1, 0}));
        v.push_back(V({0, -0.5f, 0}, {0, -1, 0}));
        idx.insert(idx.end(), {c, c + 1, c + 2});
    }
}

void buildCylinder(std::vector<Vertex>& v, std::vector<uint32_t>& idx, int segs) {
    for (int s = 0; s < segs; s++) {
        float a0 = TAU * s / segs, a1 = TAU * (s + 1) / segs;
        vec3 d0(std::sin(a0), 0, std::cos(a0)), d1(std::sin(a1), 0, std::cos(a1));
        uint32_t b = (uint32_t)v.size();
        v.push_back(V(d0 * 0.5f + vec3(0, -0.5f, 0), d0));
        v.push_back(V(d1 * 0.5f + vec3(0, -0.5f, 0), d1));
        v.push_back(V(d1 * 0.5f + vec3(0, 0.5f, 0), d1));
        v.push_back(V(d0 * 0.5f + vec3(0, 0.5f, 0), d0));
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        uint32_t t = (uint32_t)v.size();
        v.push_back(V({0, 0.5f, 0}, {0, 1, 0}));
        v.push_back(V(d0 * 0.5f + vec3(0, 0.5f, 0), {0, 1, 0}));
        v.push_back(V(d1 * 0.5f + vec3(0, 0.5f, 0), {0, 1, 0}));
        idx.insert(idx.end(), {t, t + 1, t + 2});
        uint32_t u = (uint32_t)v.size();
        v.push_back(V({0, -0.5f, 0}, {0, -1, 0}));
        v.push_back(V(d1 * 0.5f + vec3(0, -0.5f, 0), {0, -1, 0}));
        v.push_back(V(d0 * 0.5f + vec3(0, -0.5f, 0), {0, -1, 0}));
        idx.insert(idx.end(), {u, u + 1, u + 2});
    }
}

static uint16_t toHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7C00);
    return (uint16_t)(sign | (exp << 10) | (mant >> 13));
}

// ---------------------------------------------------------------------------
// Instance / device
// ---------------------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT sev, VkDebugUtilsMessageTypeFlagsEXT type,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && !(type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT))
        std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
    return VK_FALSE;
}

bool Renderer::createInstance(bool validation) {
    if (!vkLoadGlobal((PFN_vkGetInstanceProcAddr)glfwGetInstanceProcAddress(nullptr, "vkGetInstanceProcAddr"))) return false;
    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    if (!glfwExts) {
        std::fprintf(stderr, "GLFW could not find Vulkan surface extensions\n");
        return false;
    }
    std::vector<const char*> exts(glfwExts, glfwExts + extCount);
    std::vector<const char*> layers;
    if (validation) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> props(n);
        vkEnumerateInstanceLayerProperties(&n, props.data());
        for (auto& p : props)
            if (std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0) layers.push_back("VK_LAYER_KHRONOS_validation");
        if (layers.empty()) std::fprintf(stderr, "Validation layer requested but not installed\n");
        else exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Mythbound";
    app.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
    app.pEngineName = "Mythbound Engine";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = (uint32_t)layers.size();
    ci.ppEnabledLayerNames = layers.data();
    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));
    if (!vkLoadInstance(instance_)) return false;
    if (!layers.empty()) {
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkLoadInstanceFunc(instance_, "vkCreateDebugUtilsMessengerEXT");
        if (create) {
            VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = debugCallback;
            create(instance_, &mi, nullptr, &messenger_);
        }
    }
    return true;
}

bool Renderer::pickDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    if (n == 0) {
        std::fprintf(stderr, "No Vulkan GPU found\n");
        return false;
    }
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(instance_, &n, gpus.data());
    int bestScore = -1;
    for (auto g : gpus) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qp(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qn, qp.data());
        for (uint32_t q = 0; q < qn; q++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(g, q, surface_, &present);
            if (!(qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(g, &props);
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestScore) {
                bestScore = score;
                gpu_ = g;
                queueFamily_ = q;
                std::snprintf(deviceName_, sizeof(deviceName_), "%s", props.deviceName);
                VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
                samples_ = (counts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
            }
            break;
        }
    }
    if (!gpu_) {
        std::fprintf(stderr, "No GPU can present to this window\n");
        return false;
    }
    if (const char* env = std::getenv("MYTHBOUND_MSAA"))
        if (env[0] == '0' || env[0] == '1') samples_ = VK_SAMPLE_COUNT_1_BIT;
    vkGetPhysicalDeviceMemoryProperties(gpu_, &memProps_);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queueFamily_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceFeatures feats{};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    dci.pEnabledFeatures = &feats;
    VK_CHECK(vkCreateDevice(gpu_, &dci, nullptr, &device_));
    if (!vkLoadDevice(device_)) return false;
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return true;
}

uint32_t Renderer::findMemory(uint32_t typeBits, VkMemoryPropertyFlags props) {
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; i++)
        if ((typeBits & (1u << i)) && (memProps_.memoryTypes[i].propertyFlags & props) == props) return i;
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; i++)
        if (typeBits & (1u << i)) return i;
    return 0;
}

Renderer::Buffer Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device_, &bi, nullptr, &b.buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, b.buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(req.memoryTypeBits, props);
    vkAllocateMemory(device_, &ai, nullptr, &b.mem);
    vkBindBufferMemory(device_, b.buf, b.mem, 0);
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) vkMapMemory(device_, b.mem, 0, size, 0, &b.mapped);
    return b;
}

void Renderer::destroyBuffer(Buffer& b) {
    if (b.mapped) vkUnmapMemory(device_, b.mem);
    if (b.buf) vkDestroyBuffer(device_, b.buf, nullptr);
    if (b.mem) vkFreeMemory(device_, b.mem, nullptr);
    b = Buffer{};
}

VkCommandBuffer Renderer::beginOneShot() {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void Renderer::endOneShot(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
}

Renderer::Buffer Renderer::createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(staging.mapped, data, (size_t)size);
    Buffer dst = createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkCommandBuffer cmd = beginOneShot();
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging.buf, dst.buf, 1, &region);
    endOneShot(cmd);
    destroyBuffer(staging);
    return dst;
}

Renderer::Image Renderer::createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                                      VkSampleCountFlagBits samples) {
    Image im;
    im.w = w;
    im.h = h;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {w, h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = samples;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(device_, &ii, nullptr, &im.img);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, im.img, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &ai, nullptr, &im.mem);
    vkBindImageMemory(device_, im.img, im.mem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange = {aspect, 0, 1, 0, 1};
    vkCreateImageView(device_, &vi, nullptr, &im.view);
    return im;
}

void Renderer::destroyImage(Image& i) {
    if (i.view) vkDestroyImageView(device_, i.view, nullptr);
    if (i.img) vkDestroyImage(device_, i.img, nullptr);
    if (i.mem) vkFreeMemory(device_, i.mem, nullptr);
    i = Image{};
}

Renderer::Image Renderer::uploadImage(uint32_t w, uint32_t h, VkFormat fmt, const void* data, size_t bytes) {
    Image im = createImage(w, h, fmt, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Buffer staging = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(staging.mapped, data, bytes);
    VkCommandBuffer cmd = beginOneShot();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = im.img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging.buf, im.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    endOneShot(cmd);
    destroyBuffer(staging);
    return im;
}

Renderer::MeshGPU Renderer::uploadMesh(const std::vector<Vertex>& v, const std::vector<uint32_t>& idx) {
    MeshGPU m;
    if (v.empty() || idx.empty()) return m;
    m.vb = createDeviceBuffer(v.data(), v.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m.ib = createDeviceBuffer(idx.data(), idx.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    m.indexCount = (uint32_t)idx.size();
    return m;
}

void Renderer::destroyMesh(MeshGPU& m) {
    destroyBuffer(m.vb);
    destroyBuffer(m.ib);
    m.indexCount = 0;
}

int Renderer::addMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx) {
    meshes_.push_back(uploadMesh(verts, idx));
    return (int)meshes_.size() - 1;
}

// ---------------------------------------------------------------------------
// Swapchain and render targets
// ---------------------------------------------------------------------------
bool Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps);
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(window_, &fw, &fh);
    while (fw == 0 || fh == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &fw, &fh);
    }
    if (caps.currentExtent.width != UINT32_MAX) extent_ = caps.currentExtent;
    else extent_ = {(uint32_t)fw, (uint32_t)fh};
    extent_.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, extent_.width));
    extent_.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, extent_.height));

    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &nf, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(nf);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &nf, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    swapFormat_ = chosen.format;

    uint32_t np = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &np, nullptr);
    std::vector<VkPresentModeKHR> modes(np);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &np, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;

    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && count > caps.maxImageCount) count = caps.maxImageCount;
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_;
    sci.minImageCount = count;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = extent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) sci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = mode;
    sci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_));
    uint32_t ni = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &ni, nullptr);
    swapImages_.resize(ni);
    vkGetSwapchainImagesKHR(device_, swapchain_, &ni, swapImages_.data());
    swapViews_.resize(ni);
    for (uint32_t i = 0; i < ni; i++) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = swapImages_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swapFormat_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &swapViews_[i]));
    }
    renderFinished_.resize(ni);
    for (auto& s : renderFinished_) {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &si, nullptr, &s));
    }
    if (finalPass_) {
        swapFbs_.resize(ni);
        for (uint32_t i = 0; i < ni; i++) {
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = finalPass_;
            fi.attachmentCount = 1;
            fi.pAttachments = &swapViews_[i];
            fi.width = extent_.width;
            fi.height = extent_.height;
            fi.layers = 1;
            VK_CHECK(vkCreateFramebuffer(device_, &fi, nullptr, &swapFbs_[i]));
        }
    }
    return true;
}

void Renderer::destroySwapchain() {
    for (auto fb : swapFbs_) vkDestroyFramebuffer(device_, fb, nullptr);
    swapFbs_.clear();
    for (auto v : swapViews_) vkDestroyImageView(device_, v, nullptr);
    swapViews_.clear();
    for (auto s : renderFinished_) vkDestroySemaphore(device_, s, nullptr);
    renderFinished_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

bool Renderer::createTargets() {
    uint32_t w = extent_.width, h = extent_.height;
    hdr_ = createImage(w, h, hdrFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    std::vector<VkImageView> att;
    if (samples_ != VK_SAMPLE_COUNT_1_BIT) {
        msaaColor_ = createImage(w, h, hdrFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, samples_);
        msaaDepth_ = createImage(w, h, depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, samples_);
        att = {msaaColor_.view, msaaDepth_.view, hdr_.view};
    } else {
        msaaDepth_ = createImage(w, h, depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        att = {hdr_.view, msaaDepth_.view};
    }
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = mainPass_;
    fi.attachmentCount = (uint32_t)att.size();
    fi.pAttachments = att.data();
    fi.width = w;
    fi.height = h;
    fi.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device_, &fi, nullptr, &mainFb_));
    for (int i = 0; i < BLOOM_LEVELS; i++) {
        uint32_t bw = std::max(1u, w >> (i + 1)), bh = std::max(1u, h >> (i + 1));
        bloom_[i] = createImage(bw, bh, hdrFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        VkFramebufferCreateInfo bi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        bi.renderPass = bloomPass_;
        bi.attachmentCount = 1;
        bi.pAttachments = &bloom_[i].view;
        bi.width = bw;
        bi.height = bh;
        bi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device_, &bi, nullptr, &bloomFb_[i]));
    }
    for (int k = 0; k < 2; k++) {
        Image& im = k == 0 ? ao_ : rays_;
        VkFramebuffer& fb = k == 0 ? aoFb_ : raysFb_;
        im = createImage(std::max(1u, w / 2), std::max(1u, h / 2), hdrFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT);
        VkFramebufferCreateInfo bi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        bi.renderPass = bloomPass_;
        bi.attachmentCount = 1;
        bi.pAttachments = &im.view;
        bi.width = im.w;
        bi.height = im.h;
        bi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device_, &bi, nullptr, &fb));
    }
    return true;
}

void Renderer::destroyTargets() {
    if (mainFb_) vkDestroyFramebuffer(device_, mainFb_, nullptr);
    mainFb_ = VK_NULL_HANDLE;
    for (int i = 0; i < BLOOM_LEVELS; i++) {
        if (bloomFb_[i]) vkDestroyFramebuffer(device_, bloomFb_[i], nullptr);
        bloomFb_[i] = VK_NULL_HANDLE;
        destroyImage(bloom_[i]);
    }
    for (VkFramebuffer* fb : {&aoFb_, &raysFb_}) {
        if (*fb) vkDestroyFramebuffer(device_, *fb, nullptr);
        *fb = VK_NULL_HANDLE;
    }
    destroyImage(ao_);
    destroyImage(rays_);
    destroyImage(msaaColor_);
    destroyImage(msaaDepth_);
    destroyImage(hdr_);
}

// ---------------------------------------------------------------------------
// Render passes
// ---------------------------------------------------------------------------
static VkSubpassDependency dep(uint32_t src, uint32_t dst, VkPipelineStageFlags ss, VkPipelineStageFlags ds, VkAccessFlags sa, VkAccessFlags da) {
    VkSubpassDependency d{};
    d.srcSubpass = src;
    d.dstSubpass = dst;
    d.srcStageMask = ss;
    d.dstStageMask = ds;
    d.srcAccessMask = sa;
    d.dstAccessMask = da;
    return d;
}

bool Renderer::createRenderPasses() {
    const VkPipelineStageFlags allGfx = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    // Shadow atlas.
    {
        VkAttachmentDescription a{};
        a.format = depthFormat_;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkAttachmentReference r{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription s{};
        s.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        s.pDepthStencilAttachment = &r;
        VkSubpassDependency d[2] = {
            dep(VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
            dep(0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)};
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1;
        rp.pAttachments = &a;
        rp.subpassCount = 1;
        rp.pSubpasses = &s;
        rp.dependencyCount = 2;
        rp.pDependencies = d;
        VK_CHECK(vkCreateRenderPass(device_, &rp, nullptr, &shadowPass_));
    }
    // Main HDR pass (optionally multisampled with resolve).
    {
        bool ms = samples_ != VK_SAMPLE_COUNT_1_BIT;
        VkAttachmentDescription a[3]{};
        for (auto& x : a) {
            x.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            x.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            x.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        a[0].format = hdrFormat_;
        a[0].samples = samples_;
        a[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a[0].storeOp = ms ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
        a[0].finalLayout = ms ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        a[1].format = depthFormat_;
        a[1].samples = samples_;
        a[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        a[2].format = hdrFormat_;
        a[2].samples = VK_SAMPLE_COUNT_1_BIT;
        a[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkAttachmentReference rref{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription s{};
        s.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        s.colorAttachmentCount = 1;
        s.pColorAttachments = &cref;
        s.pDepthStencilAttachment = &dref;
        if (ms) s.pResolveAttachments = &rref;
        VkSubpassDependency d[2] = {
            dep(VK_SUBPASS_EXTERNAL, 0, allGfx, allGfx, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT),
            dep(0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)};
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = ms ? 3 : 2;
        rp.pAttachments = a;
        rp.subpassCount = 1;
        rp.pSubpasses = &s;
        rp.dependencyCount = 2;
        rp.pDependencies = d;
        VK_CHECK(vkCreateRenderPass(device_, &rp, nullptr, &mainPass_));
    }
    // Bloom passes (overwrite / accumulate) and the final swapchain pass.
    auto colorPass = [&](VkFormat fmt, bool load, VkImageLayout finalLayout, VkRenderPass* out) {
        VkAttachmentDescription a{};
        a.format = fmt;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = load ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = finalLayout;
        VkAttachmentReference r{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription s{};
        s.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        s.colorAttachmentCount = 1;
        s.pColorAttachments = &r;
        VkSubpassDependency d[2] = {
            dep(VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT),
            dep(0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)};
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1;
        rp.pAttachments = &a;
        rp.subpassCount = 1;
        rp.pSubpasses = &s;
        rp.dependencyCount = 2;
        rp.pDependencies = d;
        return vkCreateRenderPass(device_, &rp, nullptr, out) == VK_SUCCESS;
    };
    if (!colorPass(hdrFormat_, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &bloomPass_)) return false;
    if (!colorPass(hdrFormat_, true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &bloomLoadPass_)) return false;
    if (!colorPass(swapFormat_, false, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &finalPass_)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------
VkShaderModule Renderer::shaderModule(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device_, &ci, nullptr, &m);
    return m;
}

VkPipeline Renderer::buildPipeline(const PipeCfg& c) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = c.vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = c.fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindings[2]{};
    VkVertexInputAttributeDescription attrs[13]{};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (c.input == 1 || c.input == 2) {
        bindings[0] = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
        attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)};
        attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, skin)};
        attrs[4] = {4, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, sway)};
        attrs[5] = {11, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
        attrs[6] = {12, 0, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, card)};
        uint32_t na = 7, nb = 1;
        if (c.input == 1) {
            bindings[1] = {1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE};
            for (uint32_t k = 0; k < 4; k++) attrs[7 + k] = {5 + k, 1, VK_FORMAT_R32G32B32A32_SFLOAT, k * 16};
            attrs[11] = {9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, color)};
            attrs[12] = {10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, params)};
            na = 13;
            nb = 2;
        }
        vi.vertexBindingDescriptionCount = nb;
        vi.pVertexBindingDescriptions = bindings;
        vi.vertexAttributeDescriptionCount = na;
        vi.pVertexAttributeDescriptions = attrs;
    } else if (c.input == 3 || c.input == 4) {
        uint32_t stride = c.input == 3 ? sizeof(ParticleInst) : sizeof(UIQuad);
        bindings[0] = {0, stride, VK_VERTEX_INPUT_RATE_INSTANCE};
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16};
        attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = bindings;
        vi.vertexAttributeDescriptionCount = c.input == 4 ? 3 : 2;
        vi.pVertexAttributeDescriptions = attrs;
    }
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = c.cull;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    if (c.depthBias) {
        rs.depthBiasEnable = VK_TRUE;
        rs.depthBiasConstantFactor = 1.25f;
        rs.depthBiasSlopeFactor = 1.75f;
    }
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = c.samples;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = c.depthTest;
    ds.depthWriteEnable = c.depthWrite;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = c.rgbOnly ? 0x7 : 0xF;
    if (c.blend) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = c.blend == 2 ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = c.colorOutput ? 1 : 0;
    cb.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount = c.fs ? 2 : 1;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dy;
    pi.layout = c.layout;
    pi.renderPass = c.pass;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &p) != VK_SUCCESS) std::fprintf(stderr, "Failed to create pipeline\n");
    return p;
}

bool Renderer::createPipelines() {
    // Scene set: UBO, shadow atlas, bone palette, height map, grass map.
    VkDescriptorSetLayoutBinding b[5]{};
    VkShaderStageFlags vf = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    b[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, vf, nullptr};
    b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    b[3] = {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, vf, nullptr};
    b[4] = {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, vf, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 5;
    li.pBindings = b;
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &li, nullptr, &sceneSetLayout_));
    VkDescriptorSetLayoutBinding pb[4]{};
    for (uint32_t i = 0; i < 4; i++) pb[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    li.bindingCount = 4;
    li.pBindings = pb;
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &li, nullptr, &postSetLayout_));

    VkPushConstantRange pcr{vf, 0, 16};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &sceneSetLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &sceneLayout_));
    pl.pSetLayouts = &postSetLayout_;
    VkPushConstantRange postRange{vf, 0, 64};
    pl.pPushConstantRanges = &postRange;
    VK_CHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &postLayout_));
    pl.pPushConstantRanges = &pcr;
    pl.setLayoutCount = 0;
    VK_CHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &uiLayout_));

#define MOD(name) shaderModule(spv_##name, sizeof(spv_##name))
    VkShaderModule litV = MOD(lit_vert), skinV = MOD(skin_vert), litF = MOD(lit_frag);
    VkShaderModule terV = MOD(terrain_vert), terF = MOD(terrain_frag);
    VkShaderModule shV = MOD(shadow_vert), shSkV = MOD(shadow_skin_vert);
    VkShaderModule grV = MOD(grass_vert), grF = MOD(grass_frag);
    VkShaderModule waV = MOD(water_vert), waF = MOD(water_frag);
    VkShaderModule skV = MOD(sky_vert), skF = MOD(sky_frag);
    VkShaderModule paV = MOD(particle_vert), paF = MOD(particle_frag);
    VkShaderModule ssF = MOD(ssao_frag), raF = MOD(rays_frag), shF = MOD(shadow_frag);
    VkShaderModule poV = MOD(post_vert), bdF = MOD(bloom_down_frag), buF = MOD(bloom_up_frag), coF = MOD(composite_frag);
    VkShaderModule uiV = MOD(ui_vert), uiF = MOD(ui_frag);
#undef MOD

    PipeCfg c;
    c.layout = sceneLayout_;
    c.pass = shadowPass_;
    c.input = 1;
    c.depthTest = c.depthWrite = true;
    c.depthBias = true;
    c.colorOutput = false;
    c.vs = shV;
    c.fs = shF;
    shadowPipe_ = buildPipeline(c);
    c.vs = shSkV;
    shadowSkinPipe_ = buildPipeline(c);

    c = PipeCfg{};
    c.layout = sceneLayout_;
    c.pass = mainPass_;
    c.samples = samples_;
    c.input = 1;
    c.depthTest = c.depthWrite = true;
    c.cull = VK_CULL_MODE_BACK_BIT;
    c.vs = terV;
    c.fs = terF;
    terrainPipe_ = buildPipeline(c);
    c.vs = litV;
    c.fs = litF;
    c.cull = VK_CULL_MODE_NONE;  // leaves and wing membranes are visible from both sides
    litPipe_ = buildPipeline(c);
    c.vs = skinV;
    skinPipe_ = buildPipeline(c);
    c.input = 2;
    c.vs = grV;
    c.fs = grF;
    grassPipe_ = buildPipeline(c);
    c.vs = waV;
    c.fs = waF;
    c.depthWrite = false;
    c.blend = 1;
    c.rgbOnly = true;
    waterPipe_ = buildPipeline(c);
    c.rgbOnly = false;
    c.input = 0;
    c.vs = skV;
    c.fs = skF;
    c.depthTest = false;
    c.blend = 0;
    skyPipe_ = buildPipeline(c);
    c.input = 3;
    c.vs = paV;
    c.fs = paF;
    c.depthTest = true;
    c.depthWrite = false;
    c.blend = 1;
    c.rgbOnly = true;
    particleAlphaPipe_ = buildPipeline(c);
    c.blend = 2;
    particleAddPipe_ = buildPipeline(c);

    c = PipeCfg{};
    c.layout = postLayout_;
    c.pass = bloomPass_;
    c.vs = poV;
    c.fs = ssF;
    ssaoPipe_ = buildPipeline(c);
    c.fs = raF;
    raysPipe_ = buildPipeline(c);
    c.fs = bdF;
    bloomDownPipe_ = buildPipeline(c);
    c.fs = buF;
    c.blend = 2;
    bloomUpPipe_ = buildPipeline(c);
    c.pass = finalPass_;
    c.fs = coF;
    c.blend = 0;
    compositePipe_ = buildPipeline(c);
    c.layout = uiLayout_;
    c.input = 4;
    c.vs = uiV;
    c.fs = uiF;
    c.blend = 1;
    uiPipe_ = buildPipeline(c);

    for (auto m : {litV, skinV, litF, terV, terF, shV, shSkV, grV, grF, waV, waF, skV, skF, paV, paF, poV, bdF, buF, coF, uiV, uiF, ssF, raF, shF})
        vkDestroyShaderModule(device_, m, nullptr);
    return shadowPipe_ && shadowSkinPipe_ && terrainPipe_ && litPipe_ && skinPipe_ && grassPipe_ && waterPipe_ && skyPipe_ && particleAddPipe_ &&
           particleAlphaPipe_ && ssaoPipe_ && raysPipe_ && bloomDownPipe_ && bloomUpPipe_ && compositePipe_ && uiPipe_;
}

bool Renderer::createStatic() {
    // Shadow atlas: left half = near cascade, right half = far cascade.
    shadow_ = createImage(SHADOW_W, SHADOW_H, depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT);
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = shadowPass_;
    fi.attachmentCount = 1;
    fi.pAttachments = &shadow_.view;
    fi.width = SHADOW_W;
    fi.height = SHADOW_H;
    fi.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device_, &fi, nullptr, &shadowFb_));

    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(gpu_, depthFormat_, &fp);
    bool linear = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    si.maxLod = 1.0f;
    VK_CHECK(vkCreateSampler(device_, &si, nullptr, &shadowSampler_));
    VkSamplerCreateInfo ls{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ls.magFilter = ls.minFilter = VK_FILTER_LINEAR;
    ls.addressModeU = ls.addressModeV = ls.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ls.maxLod = 1.0f;
    VK_CHECK(vkCreateSampler(device_, &ls, nullptr, &linearSampler_));

    // Placeholder terrain maps until the island uploads real ones.
    uint16_t h0 = toHalf(-30.0f);
    heightTex_ = uploadImage(1, 1, VK_FORMAT_R16_SFLOAT, &h0, 2);
    uint32_t g0 = 0;
    grassTex_ = uploadImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM, &g0, 4);

    const uint32_t postSets = BLOOM_LEVELS * 2 + 3;
    VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FRAMES},
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FRAMES * 3 + postSets * 4},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, FRAMES}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = FRAMES + postSets;
    dp.poolSizeCount = 3;
    dp.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(device_, &dp, nullptr, &descPool_));
    std::vector<VkDescriptorSetLayout> layouts(postSets, postSetLayout_);
    std::vector<VkDescriptorSet> sets(layouts.size());
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descPool_;
    dai.descriptorSetCount = (uint32_t)layouts.size();
    dai.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device_, &dai, sets.data()));
    for (int i = 0; i < BLOOM_LEVELS; i++) {
        downSets_[i] = sets[i];
        upSets_[i] = sets[BLOOM_LEVELS + i];
    }
    compositeSet_ = sets[BLOOM_LEVELS * 2];
    aoSet_ = sets[BLOOM_LEVELS * 2 + 1];
    raysSet_ = sets[BLOOM_LEVELS * 2 + 2];
    return true;
}

void Renderer::writePostSets() {
    std::vector<VkDescriptorImageInfo> infos;
    std::vector<VkWriteDescriptorSet> writes;
    infos.reserve(128);
    auto write = [&](VkDescriptorSet set, uint32_t binding, VkImageView view) {
        infos.push_back({linearSampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &infos.back();
        writes.push_back(w);
    };
    auto set4 = [&](VkDescriptorSet set, VkImageView a, VkImageView b, VkImageView c, VkImageView d) {
        write(set, 0, a);
        write(set, 1, b);
        write(set, 2, c);
        write(set, 3, d);
    };
    infos.reserve(128);
    for (int i = 0; i < BLOOM_LEVELS; i++) {
        set4(downSets_[i], i == 0 ? hdr_.view : bloom_[i - 1].view, hdr_.view, hdr_.view, hdr_.view);
        set4(upSets_[i], bloom_[std::min(i + 1, BLOOM_LEVELS - 1)].view, hdr_.view, hdr_.view, hdr_.view);
    }
    set4(compositeSet_, hdr_.view, bloom_[0].view, ao_.view, rays_.view);
    set4(aoSet_, hdr_.view, hdr_.view, hdr_.view, hdr_.view);
    set4(raysSet_, hdr_.view, hdr_.view, hdr_.view, hdr_.view);
    vkUpdateDescriptorSets(device_, (uint32_t)writes.size(), writes.data(), 0, nullptr);
}

void Renderer::writeSceneSets() {
    for (auto& f : frames_) {
        if (!f.set) continue;
        VkDescriptorBufferInfo ub{f.ubo.buf, 0, sizeof(SceneUBO)};
        VkDescriptorBufferInfo bb{f.bones.buf, 0, VK_WHOLE_SIZE};
        VkDescriptorImageInfo sh{shadowSampler_, shadow_.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo hi{linearSampler_, heightTex_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo gi{linearSampler_, grassTex_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[5]{};
        for (int i = 0; i < 5; i++) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = f.set;
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].pBufferInfo = &ub;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].pImageInfo = &sh;
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[2].pBufferInfo = &bb;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[3].pImageInfo = &hi;
        w[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[4].pImageInfo = &gi;
        vkUpdateDescriptorSets(device_, 5, w, 0, nullptr);
    }
}

bool Renderer::createFrames() {
    const VkMemoryPropertyFlags hostVis = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (auto& f : frames_) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = cmdPool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &f.cmd));
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &f.imageAvailable));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(device_, &fci, nullptr, &f.fence));
        f.ubo = createBuffer(sizeof(SceneUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, hostVis);
        f.instances = createBuffer(sizeof(Instance) * MAX_INSTANCES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostVis);
        f.bones = createBuffer(sizeof(mat4) * MAX_BONES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hostVis);
        f.particles = createBuffer(sizeof(ParticleInst) * MAX_PARTICLES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostVis);
        f.ui = createBuffer(sizeof(UIQuad) * MAX_UI, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostVis);
        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descPool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &sceneSetLayout_;
        VK_CHECK(vkAllocateDescriptorSets(device_, &dai, &f.set));
    }
    writeSceneSets();
    return true;
}

void Renderer::createBuiltinMeshes() {
    for (int m = 0; m < MESH_BUILTIN_COUNT; m++) {
        std::vector<Vertex> v;
        std::vector<uint32_t> i;
        switch (m) {
            case MESH_CUBE: buildCube(v, i); break;
            case MESH_SPHERE: buildSphere(v, i, 12, 16); break;
            case MESH_CONE: buildCone(v, i, 12); break;
            case MESH_CYL: buildCylinder(v, i, 12); break;
        }
        meshes_.push_back(uploadMesh(v, i));
    }
    // Ocean grid (denser near the middle), recentred on the camera in the vertex shader.
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    const int N = 200;
    const float S = 3200.0f;
    auto warp = [](float t) { return (t < 0 ? -1.0f : 1.0f) * t * t; };
    for (int z = 0; z <= N; z++)
        for (int x = 0; x <= N; x++) {
            float fx = warp(2.0f * x / N - 1.0f), fz = warp(2.0f * z / N - 1.0f);
            v.push_back(V({fx * S * 0.5f, 0, fz * S * 0.5f}, {0, 1, 0}));
        }
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            uint32_t a = z * (N + 1) + x, b = a + N + 1;
            idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    water_ = uploadMesh(v, idx);
    // A single grass blade: 3 segments tapering to a tip; x = side (-0.5..0.5), y = height 0..1.
    v.clear();
    idx.clear();
    for (int k = 0; k <= 3; k++) {
        float t = k / 3.0f;
        float w = 1.0f - t * 0.85f;
        v.push_back(V({-0.5f * w, t, 0}, {0, 0, 1}));
        v.push_back(V({0.5f * w, t, 0}, {0, 0, 1}));
    }
    for (uint32_t k = 0; k < 3; k++) {
        uint32_t a = k * 2;
        idx.insert(idx.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
    }
    grassBlade_ = uploadMesh(v, idx);
}

bool Renderer::init(GLFWwindow* window, bool validation) {
    window_ = window;
    if (!createInstance(validation)) return false;
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
        std::fprintf(stderr, "Failed to create window surface\n");
        return false;
    }
    if (!pickDevice()) return false;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(gpu_, VK_FORMAT_D32_SFLOAT, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) depthFormat_ = VK_FORMAT_D16_UNORM;
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = queueFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &cp, nullptr, &cmdPool_));

    if (!createSwapchain()) return false;  // first, to learn the swapchain format
    if (!createRenderPasses()) return false;
    destroySwapchain();
    if (!createSwapchain()) return false;
    if (!createPipelines()) return false;
    if (!createStatic()) return false;
    if (!createTargets()) return false;
    writePostSets();
    if (!createFrames()) return false;
    createBuiltinMeshes();
    std::printf("Renderer ready on %s (%ux%u, %dx MSAA)\n", deviceName_, extent_.width, extent_.height, (int)samples_);
    return true;
}

void Renderer::uploadTerrain(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx) {
    if (hasTerrain_) {
        vkDeviceWaitIdle(device_);
        destroyMesh(terrain_);
    }
    terrain_ = uploadMesh(verts, idx);
    hasTerrain_ = true;
}

void Renderer::uploadTerrainMaps(int n, const std::vector<float>& heights, const std::vector<uint32_t>& grassRGBA) {
    vkDeviceWaitIdle(device_);
    destroyImage(heightTex_);
    destroyImage(grassTex_);
    std::vector<uint16_t> half(heights.size());
    for (size_t i = 0; i < heights.size(); i++) half[i] = toHalf(heights[i]);
    heightTex_ = uploadImage(n, n, VK_FORMAT_R16_SFLOAT, half.data(), half.size() * 2);
    grassTex_ = uploadImage(n, n, VK_FORMAT_R8G8B8A8_UNORM, grassRGBA.data(), grassRGBA.size() * 4);
    writeSceneSets();
}

void Renderer::shutdown() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    for (auto& f : frames_) {
        destroyBuffer(f.ubo);
        destroyBuffer(f.instances);
        destroyBuffer(f.bones);
        destroyBuffer(f.particles);
        destroyBuffer(f.ui);
        vkDestroySemaphore(device_, f.imageAvailable, nullptr);
        vkDestroyFence(device_, f.fence, nullptr);
    }
    for (auto& m : meshes_) destroyMesh(m);
    destroyMesh(water_);
    destroyMesh(grassBlade_);
    if (hasTerrain_) destroyMesh(terrain_);
    destroyTargets();
    vkDestroyFramebuffer(device_, shadowFb_, nullptr);
    destroyImage(shadow_);
    destroyImage(heightTex_);
    destroyImage(grassTex_);
    destroySwapchain();
    for (auto p : {shadowPipe_, shadowSkinPipe_, terrainPipe_, litPipe_, skinPipe_, grassPipe_, waterPipe_, skyPipe_, particleAddPipe_, ssaoPipe_, raysPipe_,
                   particleAlphaPipe_, bloomDownPipe_, bloomUpPipe_, compositePipe_, uiPipe_})
        vkDestroyPipeline(device_, p, nullptr);
    vkDestroyPipelineLayout(device_, sceneLayout_, nullptr);
    vkDestroyPipelineLayout(device_, postLayout_, nullptr);
    vkDestroyPipelineLayout(device_, uiLayout_, nullptr);
    vkDestroyDescriptorPool(device_, descPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, sceneSetLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, postSetLayout_, nullptr);
    vkDestroySampler(device_, shadowSampler_, nullptr);
    vkDestroySampler(device_, linearSampler_, nullptr);
    for (auto rp : {shadowPass_, mainPass_, bloomPass_, bloomLoadPass_, finalPass_}) vkDestroyRenderPass(device_, rp, nullptr);
    vkDestroyCommandPool(device_, cmdPool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (messenger_) {
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkLoadInstanceFunc(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy) destroy(instance_, messenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
    device_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Per-frame rendering
// ---------------------------------------------------------------------------
namespace {
struct DrawRange {
    uint32_t first = 0, count = 0;
};
std::vector<DrawRange> g_static, g_far, g_skinned;
DrawRange g_pAlpha, g_pAdd;
}  // namespace

void Renderer::drawMeshes(VkCommandBuffer cmd, Frame& f, FrameScene& scene, VkPipeline staticPipe, VkPipeline skinPipe, VkPipeline terrainPipe, bool far) {
    VkDeviceSize offs[2] = {0, 0};
    if (hasTerrain_ && scene.drawTerrain) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipe);
        VkBuffer bufs[2] = {terrain_.vb.buf, f.instances.buf};
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
        vkCmdBindIndexBuffer(cmd, terrain_.ib.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, terrain_.indexCount, 1, 0, 0, 0);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, staticPipe);
    for (size_t m = 0; m < meshes_.size(); m++) {
        if (!g_static[m].count || !meshes_[m].indexCount) continue;
        VkBuffer bufs[2] = {meshes_[m].vb.buf, f.instances.buf};
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
        vkCmdBindIndexBuffer(cmd, meshes_[m].ib.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, meshes_[m].indexCount, g_static[m].count, 0, 0, g_static[m].first);
    }
    for (size_t m = 0; far && m < meshes_.size(); m++) {
        if (!g_far[m].count || !meshes_[m].indexCount) continue;
        VkBuffer bufs[2] = {meshes_[m].vb.buf, f.instances.buf};
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
        vkCmdBindIndexBuffer(cmd, meshes_[m].ib.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, meshes_[m].indexCount, g_far[m].count, 0, 0, g_far[m].first);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinPipe);
    for (size_t m = 0; m < meshes_.size(); m++) {
        if (!g_skinned[m].count || !meshes_[m].indexCount) continue;
        VkBuffer bufs[2] = {meshes_[m].vb.buf, f.instances.buf};
        vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
        vkCmdBindIndexBuffer(cmd, meshes_[m].ib.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, meshes_[m].indexCount, g_skinned[m].count, 0, 0, g_skinned[m].first);
    }
}

void Renderer::recordFrame(Frame& f, uint32_t imageIndex, FrameScene& scene) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkCommandBuffer cmd = f.cmd;
    vkBeginCommandBuffer(cmd, &bi);
    VkShaderStageFlags pcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    auto viewport = [&](float x, float y, float w, float h) {
        VkViewport vp{x, y, w, h, 0, 1};
        VkRect2D sc{{(int32_t)x, (int32_t)y}, {(uint32_t)w, (uint32_t)h}};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
    };

    // 1. Shadow cascades.
    {
        VkClearValue clear{};
        clear.depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = shadowPass_;
        rb.framebuffer = shadowFb_;
        rb.renderArea = {{0, 0}, {SHADOW_W, SHADOW_H}};
        rb.clearValueCount = 1;
        rb.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
        if (scene.ubo.sunDir.w > 0.01f) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneLayout_, 0, 1, &f.set, 0, nullptr);
            for (int c = 0; c < 2; c++) {
                viewport((float)(c * SHADOW_H), 0, (float)SHADOW_H, (float)SHADOW_H);
                float pc[4] = {(float)c, 0, 0, 0};
                vkCmdPushConstants(cmd, sceneLayout_, pcStages, 0, 16, pc);
                drawMeshes(cmd, f, scene, shadowPipe_, shadowSkinPipe_, shadowPipe_, false);
            }
        }
        vkCmdEndRenderPass(cmd);
    }

    // 2. HDR scene.
    {
        VkClearValue clears[3]{};
        clears[0].color = {{0.02f, 0.02f, 0.03f, 1}};
        clears[1].depthStencil = {1.0f, 0};
        clears[2].color = {{0, 0, 0, 1}};
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = mainPass_;
        rb.framebuffer = mainFb_;
        rb.renderArea = {{0, 0}, extent_};
        rb.clearValueCount = samples_ != VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
        rb.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
        viewport(0, 0, (float)extent_.width, (float)extent_.height);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneLayout_, 0, 1, &f.set, 0, nullptr);
        float pc[4] = {0, (float)GRASS_N, (float)samples_, 0};
        vkCmdPushConstants(cmd, sceneLayout_, pcStages, 0, 16, pc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipe_);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        drawMeshes(cmd, f, scene, litPipe_, skinPipe_, terrainPipe_, true);

        VkDeviceSize zero = 0;
        if (scene.drawGrass && hasTerrain_) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, grassPipe_);
            vkCmdBindVertexBuffers(cmd, 0, 1, &grassBlade_.vb.buf, &zero);
            vkCmdBindIndexBuffer(cmd, grassBlade_.ib.buf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, grassBlade_.indexCount, GRASS_N * GRASS_N, 0, 0, 0);
        }
        if (scene.drawWater) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipe_);
            vkCmdBindVertexBuffers(cmd, 0, 1, &water_.vb.buf, &zero);
            vkCmdBindIndexBuffer(cmd, water_.ib.buf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, water_.indexCount, 1, 0, 0, 0);
        }
        if (g_pAlpha.count) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particleAlphaPipe_);
            vkCmdBindVertexBuffers(cmd, 0, 1, &f.particles.buf, &zero);
            vkCmdDraw(cmd, 6, g_pAlpha.count, 0, g_pAlpha.first);
        }
        if (g_pAdd.count) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particleAddPipe_);
            vkCmdBindVertexBuffers(cmd, 0, 1, &f.particles.buf, &zero);
            vkCmdDraw(cmd, 6, g_pAdd.count, 0, g_pAdd.first);
        }
        vkCmdEndRenderPass(cmd);
    }

    // 3. Screen-space ambient occlusion and god rays (half resolution).
    for (int k = 0; k < 2; k++) {
        const Image& im = k == 0 ? ao_ : rays_;
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = bloomPass_;
        rb.framebuffer = k == 0 ? aoFb_ : raysFb_;
        rb.renderArea = {{0, 0}, {im.w, im.h}};
        vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
        viewport(0, 0, (float)im.w, (float)im.h);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, k == 0 ? ssaoPipe_ : raysPipe_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, k == 0 ? &aoSet_ : &raysSet_, 0, nullptr);
        float pc[16] = {};
        if (k == 0) {
            float tanY = scene.post3.x, aspect = scene.post3.y;
            float v[8] = {tanY * aspect, tanY, 1.2f, scene.post2.w, 1.0f / hdr_.w, 1.0f / hdr_.h, scene.post3.z, 0};
            std::memcpy(pc, v, sizeof(v));
        } else {
            float v[8] = {scene.post2.x, scene.post2.y, scene.post2.z, 0.97f, scene.sunScreenColor.x, scene.sunScreenColor.y, scene.sunScreenColor.z, 0};
            std::memcpy(pc, v, sizeof(v));
        }
        vkCmdPushConstants(cmd, postLayout_, pcStages, 0, 64, pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // 4. Bloom: progressive downsample, then upsample-accumulate.
    for (int i = 0; i < BLOOM_LEVELS; i++) {
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = bloomPass_;
        rb.framebuffer = bloomFb_[i];
        rb.renderArea = {{0, 0}, {bloom_[i].w, bloom_[i].h}};
        vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
        viewport(0, 0, (float)bloom_[i].w, (float)bloom_[i].h);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomDownPipe_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &downSets_[i], 0, nullptr);
        const Image& src = i == 0 ? hdr_ : bloom_[i - 1];
        float pc[16] = {1.0f / src.w, 1.0f / src.h, i == 0 ? 1.0f : 0.0f, 0};
        vkCmdPushConstants(cmd, postLayout_, pcStages, 0, 64, pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }
    for (int i = BLOOM_LEVELS - 2; i >= 0; i--) {
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = bloomLoadPass_;
        rb.framebuffer = bloomFb_[i];
        rb.renderArea = {{0, 0}, {bloom_[i].w, bloom_[i].h}};
        vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
        viewport(0, 0, (float)bloom_[i].w, (float)bloom_[i].h);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomUpPipe_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &upSets_[i], 0, nullptr);
        const Image& src = bloom_[i + 1];
        float pc[16] = {1.0f / src.w, 1.0f / src.h, 0, 0};
        vkCmdPushConstants(cmd, postLayout_, pcStages, 0, 64, pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // 5. Composite (tonemap + grade) and UI onto the swapchain.
    {
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = finalPass_;
        rb.framebuffer = swapFbs_[imageIndex];
        rb.renderArea = {{0, 0}, extent_};
        vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
        viewport(0, 0, (float)extent_.width, (float)extent_.height);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipe_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout_, 0, 1, &compositeSet_, 0, nullptr);
        const vec4& post = scene.ubo.post;
        float pc[16] = {post.x, post.y, post.z, post.w, scene.post2.x, scene.post2.y, scene.post2.z, scene.post2.w,
                        scene.post3.z, scene.post3.w, 1.0f / extent_.width, 1.0f / extent_.height};
        vkCmdPushConstants(cmd, postLayout_, pcStages, 0, 64, pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        uint32_t uiCount = (uint32_t)std::min<size_t>(scene.ui.size(), MAX_UI);
        if (uiCount) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipe_);
            float screen[4] = {(float)extent_.width, (float)extent_.height, 0, 0};
            vkCmdPushConstants(cmd, uiLayout_, pcStages, 0, 16, screen);
            VkDeviceSize zero = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &f.ui.buf, &zero);
            vkCmdDraw(cmd, 6, uiCount, 0, 0);
        }
        vkCmdEndRenderPass(cmd);
    }
    vkEndCommandBuffer(cmd);
}

void Renderer::render(FrameScene& scene) {
    if (swapchainDirty_) {
        vkDeviceWaitIdle(device_);
        destroyTargets();
        destroySwapchain();
        createSwapchain();
        createTargets();
        writePostSets();
        swapchainDirty_ = false;
    }
    Frame& f = frames_[frameIndex_];
    vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX);
    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainDirty_ = true;
        return;
    }
    vkResetFences(device_, 1, &f.fence);

    scene.ubo.world.y = (float)GRASS_N;
    std::memcpy(f.ubo.mapped, &scene.ubo, sizeof(SceneUBO));
    Instance* dst = (Instance*)f.instances.mapped;
    dst[0] = {mat4::identity(), {1, 1, 1, 1}, {0, 0, 0, 0}};
    uint32_t cursor = 1;
    size_t meshCount = meshes_.size();
    g_static.assign(meshCount, {});
    g_far.assign(meshCount, {});
    g_skinned.assign(meshCount, {});
    auto pack = [&](std::vector<std::vector<Instance>>& lists, std::vector<DrawRange>& ranges) {
        for (size_t m = 0; m < std::min(meshCount, lists.size()); m++) {
            uint32_t n = (uint32_t)std::min<size_t>(lists[m].size(), MAX_INSTANCES - cursor);
            ranges[m] = {cursor, n};
            if (n) std::memcpy(dst + cursor, lists[m].data(), n * sizeof(Instance));
            cursor += n;
        }
    };
    pack(scene.inst, g_static);
    pack(scene.instFar, g_far);
    pack(scene.skinned, g_skinned);
    size_t nb = std::min<size_t>(scene.bones.size(), MAX_BONES);
    if (nb) std::memcpy(f.bones.mapped, scene.bones.data(), nb * sizeof(mat4));
    ParticleInst* pdst = (ParticleInst*)f.particles.mapped;
    uint32_t na = (uint32_t)std::min<size_t>(scene.particlesAlpha.size(), MAX_PARTICLES / 2);
    uint32_t nadd = (uint32_t)std::min<size_t>(scene.particlesAdd.size(), MAX_PARTICLES - na);
    if (na) std::memcpy(pdst, scene.particlesAlpha.data(), na * sizeof(ParticleInst));
    if (nadd) std::memcpy(pdst + na, scene.particlesAdd.data(), nadd * sizeof(ParticleInst));
    g_pAlpha = {0, na};
    g_pAdd = {na, nadd};
    uint32_t uiCount = (uint32_t)std::min<size_t>(scene.ui.size(), MAX_UI);
    if (uiCount) std::memcpy(f.ui.mapped, scene.ui.data(), uiCount * sizeof(UIQuad));

    vkResetCommandBuffer(f.cmd, 0);
    recordFrame(f, imageIndex, scene);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.imageAvailable;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderFinished_[imageIndex];
    vkQueueSubmit(queue_, 1, &si, f.fence);
    if (!screenshotPath_.empty()) saveScreenshot(imageIndex);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished_[imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &imageIndex;
    VkResult pr = vkQueuePresentKHR(queue_, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) swapchainDirty_ = true;
    int fw, fh;
    glfwGetFramebufferSize(window_, &fw, &fh);
    if ((uint32_t)fw != extent_.width || (uint32_t)fh != extent_.height) swapchainDirty_ = true;
    frameIndex_ = (frameIndex_ + 1) % FRAMES;
}

void Renderer::saveScreenshot(uint32_t imageIndex) {
    std::string path = screenshotPath_;
    screenshotPath_.clear();
    vkQueueWaitIdle(queue_);
    VkDeviceSize size = (VkDeviceSize)extent_.width * extent_.height * 4;
    Buffer buf = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkCommandBuffer cmd = beginOneShot();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = swapImages_[imageIndex];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent_.width, extent_.height, 1};
    vkCmdCopyImageToBuffer(cmd, swapImages_[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf.buf, 1, &region);
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = 0;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    endOneShot(cmd);
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (fp) {
        std::fprintf(fp, "P6\n%u %u\n255\n", extent_.width, extent_.height);
        const uint8_t* px = (const uint8_t*)buf.mapped;
        bool bgr = swapFormat_ == VK_FORMAT_B8G8R8A8_SRGB || swapFormat_ == VK_FORMAT_B8G8R8A8_UNORM;
        std::vector<uint8_t> row(extent_.width * 3);
        for (uint32_t y = 0; y < extent_.height; y++) {
            for (uint32_t x = 0; x < extent_.width; x++) {
                const uint8_t* p = px + (y * extent_.width + x) * 4;
                row[x * 3 + 0] = bgr ? p[2] : p[0];
                row[x * 3 + 1] = p[1];
                row[x * 3 + 2] = bgr ? p[0] : p[2];
            }
            std::fwrite(row.data(), 1, row.size(), fp);
        }
        std::fclose(fp);
        std::printf("Saved screenshot %s\n", path.c_str());
    }
    destroyBuffer(buf);
}
