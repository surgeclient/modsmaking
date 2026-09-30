#include "renderer.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

#include "lit_vert.h"
#include "lit_frag.h"
#include "shadow_vert.h"
#include "water_vert.h"
#include "water_frag.h"
#include "sky_vert.h"
#include "sky_frag.h"
#include "ui_vert.h"
#include "ui_frag.h"

#define VK_CHECK(x)                                                                   \
    do {                                                                              \
        VkResult r_ = (x);                                                            \
        if (r_ != VK_SUCCESS) {                                                       \
            std::fprintf(stderr, "Vulkan error %d at %s:%d\n", (int)r_, __FILE__, __LINE__); \
            return false;                                                             \
        }                                                                             \
    } while (0)

// ---------------------------------------------------------------------------
// Primitive meshes
// ---------------------------------------------------------------------------
static void addFace(std::vector<Vertex>& v, std::vector<uint32_t>& idx, vec3 a, vec3 b, vec3 c, vec3 d, vec3 n) {
    uint32_t base = (uint32_t)v.size();
    vec3 w(1, 1, 1);
    v.push_back({a, n, w});
    v.push_back({b, n, w});
    v.push_back({c, n, w});
    v.push_back({d, n, w});
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
            v.push_back({n * 0.5f, n, {1, 1, 1}});
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
        vec3 tip(0, 0.5f, 0);
        vec3 n0 = normalize(vec3(std::sin(a0) * std::cos(slope), std::sin(slope), std::cos(a0) * std::cos(slope)));
        vec3 n1 = normalize(vec3(std::sin(a1) * std::cos(slope), std::sin(slope), std::cos(a1) * std::cos(slope)));
        vec3 nm = normalize(vec3(std::sin(am) * std::cos(slope), std::sin(slope), std::cos(am) * std::cos(slope)));
        uint32_t b = (uint32_t)v.size();
        v.push_back({p0, n0, {1, 1, 1}});
        v.push_back({p1, n1, {1, 1, 1}});
        v.push_back({tip, nm, {1, 1, 1}});
        idx.insert(idx.end(), {b, b + 1, b + 2});
        uint32_t c = (uint32_t)v.size();
        v.push_back({p1, {0, -1, 0}, {1, 1, 1}});
        v.push_back({p0, {0, -1, 0}, {1, 1, 1}});
        v.push_back({{0, -0.5f, 0}, {0, -1, 0}, {1, 1, 1}});
        idx.insert(idx.end(), {c, c + 1, c + 2});
    }
}

void buildCylinder(std::vector<Vertex>& v, std::vector<uint32_t>& idx, int segs) {
    for (int s = 0; s < segs; s++) {
        float a0 = TAU * s / segs, a1 = TAU * (s + 1) / segs;
        vec3 d0(std::sin(a0), 0, std::cos(a0)), d1(std::sin(a1), 0, std::cos(a1));
        uint32_t b = (uint32_t)v.size();
        v.push_back({d0 * 0.5f + vec3(0, -0.5f, 0), d0, {1, 1, 1}});
        v.push_back({d1 * 0.5f + vec3(0, -0.5f, 0), d1, {1, 1, 1}});
        v.push_back({d1 * 0.5f + vec3(0, 0.5f, 0), d1, {1, 1, 1}});
        v.push_back({d0 * 0.5f + vec3(0, 0.5f, 0), d0, {1, 1, 1}});
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        uint32_t t = (uint32_t)v.size();
        v.push_back({{0, 0.5f, 0}, {0, 1, 0}, {1, 1, 1}});
        v.push_back({d0 * 0.5f + vec3(0, 0.5f, 0), {0, 1, 0}, {1, 1, 1}});
        v.push_back({d1 * 0.5f + vec3(0, 0.5f, 0), {0, 1, 0}, {1, 1, 1}});
        idx.insert(idx.end(), {t, t + 1, t + 2});
        uint32_t u = (uint32_t)v.size();
        v.push_back({{0, -0.5f, 0}, {0, -1, 0}, {1, 1, 1}});
        v.push_back({d1 * 0.5f + vec3(0, -0.5f, 0), {0, -1, 0}, {1, 1, 1}});
        v.push_back({d0 * 0.5f + vec3(0, -0.5f, 0), {0, -1, 0}, {1, 1, 1}});
        idx.insert(idx.end(), {u, u + 1, u + 2});
    }
}

// ---------------------------------------------------------------------------
// Instance / device
// ---------------------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                                    VkDebugUtilsMessageTypeFlagsEXT,
                                                    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
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
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
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
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestScore) {
                bestScore = score;
                gpu_ = g;
                queueFamily_ = q;
                std::snprintf(deviceName_, sizeof(deviceName_), "%s", props.deviceName);
            }
            break;
        }
    }
    if (!gpu_) {
        std::fprintf(stderr, "No GPU can present to this window\n");
        return false;
    }
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
    // Fall back to any compatible type (e.g. no DEVICE_LOCAL+HOST_VISIBLE combo).
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

Renderer::Buffer Renderer::createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::memcpy(staging.mapped, data, (size_t)size);
    Buffer dst = createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging.buf, dst.buf, 1, &region);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
    destroyBuffer(staging);
    return dst;
}

Renderer::Image Renderer::createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    Image im;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {w, h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
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

Renderer::MeshGPU Renderer::uploadMesh(const std::vector<Vertex>& v, const std::vector<uint32_t>& idx) {
    MeshGPU m;
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

// ---------------------------------------------------------------------------
// Swapchain
// ---------------------------------------------------------------------------
bool Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps);
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(window_, &fw, &fh);
    while (fw == 0 || fh == 0) {  // minimised
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
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    swapFormat_ = chosen.format;

    uint32_t np = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &np, nullptr);
    std::vector<VkPresentModeKHR> modes(np);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &np, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // always available (vsync)
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;  // low latency, no tearing

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
    depth_ = createImage(extent_.width, extent_.height, depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    if (mainPass_) {
        framebuffers_.resize(ni);
        for (uint32_t i = 0; i < ni; i++) {
            VkImageView att[] = {swapViews_[i], depth_.view};
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = mainPass_;
            fi.attachmentCount = 2;
            fi.pAttachments = att;
            fi.width = extent_.width;
            fi.height = extent_.height;
            fi.layers = 1;
            VK_CHECK(vkCreateFramebuffer(device_, &fi, nullptr, &framebuffers_[i]));
        }
    }
    renderFinished_.resize(ni);
    for (auto& s : renderFinished_) {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &si, nullptr, &s));
    }
    return true;
}

void Renderer::destroySwapchain() {
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    for (auto v : swapViews_) vkDestroyImageView(device_, v, nullptr);
    swapViews_.clear();
    for (auto s : renderFinished_) vkDestroySemaphore(device_, s, nullptr);
    renderFinished_.clear();
    destroyImage(depth_);
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Render passes & pipelines
// ---------------------------------------------------------------------------
bool Renderer::createRenderPasses() {
    // Main pass: swapchain colour + depth.
    VkAttachmentDescription att[2]{};
    att[0].format = swapFormat_;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    att[1].format = depthFormat_;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 2;
    rp.pAttachments = att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(device_, &rp, nullptr, &mainPass_));

    // Shadow pass: depth only, then sampled by the main pass.
    VkAttachmentDescription sd{};
    sd.format = shadowFormat_;
    sd.samples = VK_SAMPLE_COUNT_1_BIT;
    sd.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    sd.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    sd.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    sd.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    sd.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    sd.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference sref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription ssub{};
    ssub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    ssub.pDepthStencilAttachment = &sref;
    VkSubpassDependency sdeps[2]{};
    sdeps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    sdeps[0].dstSubpass = 0;
    sdeps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    sdeps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    sdeps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sdeps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    sdeps[1].srcSubpass = 0;
    sdeps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    sdeps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    sdeps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    sdeps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    sdeps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo srp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    srp.attachmentCount = 1;
    srp.pAttachments = &sd;
    srp.subpassCount = 1;
    srp.pSubpasses = &ssub;
    srp.dependencyCount = 2;
    srp.pDependencies = sdeps;
    VK_CHECK(vkCreateRenderPass(device_, &srp, nullptr, &shadowPass_));
    return true;
}

VkShaderModule Renderer::shaderModule(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device_, &ci, nullptr, &m);
    return m;
}

VkPipeline Renderer::buildPipeline(VkShaderModule vs, VkShaderModule fs, VkRenderPass pass, VkPipelineLayout layout,
                                   bool meshInput, bool depthTest, bool depthWrite, bool blend, VkCullModeFlags cull,
                                   bool depthBias, bool uiInput) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindings[2]{};
    VkVertexInputAttributeDescription attrs[8]{};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (meshInput) {
        bindings[0] = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        bindings[1] = {1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE};
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
        attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};
        for (uint32_t c = 0; c < 4; c++) attrs[3 + c] = {3 + c, 1, VK_FORMAT_R32G32B32A32_SFLOAT, c * 16};
        attrs[7] = {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, color)};
        vi.vertexBindingDescriptionCount = 2;
        vi.pVertexBindingDescriptions = bindings;
        vi.vertexAttributeDescriptionCount = 8;
        vi.pVertexAttributeDescriptions = attrs;
    } else if (uiInput) {
        bindings[0] = {0, sizeof(UIQuad), VK_VERTEX_INPUT_RATE_INSTANCE};
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = bindings;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions = attrs;
    }

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = cull;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    if (depthBias) {
        rs.depthBiasEnable = VK_TRUE;
        rs.depthBiasConstantFactor = 1.5f;
        rs.depthBiasSlopeFactor = 2.0f;
    }
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = depthTest;
    ds.depthWriteEnable = depthWrite;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xF;
    if (blend) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = fs ? 1 : 0;
    cb.pAttachments = &cba;
    if (pass == shadowPass_) cb.attachmentCount = 0;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount = fs ? 2 : 1;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dy;
    pi.layout = layout;
    pi.renderPass = pass;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &p) != VK_SUCCESS)
        std::fprintf(stderr, "Failed to create pipeline\n");
    return p;
}

bool Renderer::createPipelines() {
    VkDescriptorSetLayoutBinding b[2]{};
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1].binding = 1;
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 2;
    li.pBindings = b;
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &li, nullptr, &setLayout_));

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(device_, &pl, nullptr, &sceneLayout_));
    VkPipelineLayoutCreateInfo ul{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ul.pushConstantRangeCount = 1;
    ul.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(device_, &ul, nullptr, &uiLayout_));

    VkShaderModule litV = shaderModule(spv_lit_vert, sizeof(spv_lit_vert));
    VkShaderModule litF = shaderModule(spv_lit_frag, sizeof(spv_lit_frag));
    VkShaderModule shV = shaderModule(spv_shadow_vert, sizeof(spv_shadow_vert));
    VkShaderModule waV = shaderModule(spv_water_vert, sizeof(spv_water_vert));
    VkShaderModule waF = shaderModule(spv_water_frag, sizeof(spv_water_frag));
    VkShaderModule skV = shaderModule(spv_sky_vert, sizeof(spv_sky_vert));
    VkShaderModule skF = shaderModule(spv_sky_frag, sizeof(spv_sky_frag));
    VkShaderModule uiV = shaderModule(spv_ui_vert, sizeof(spv_ui_vert));
    VkShaderModule uiF = shaderModule(spv_ui_frag, sizeof(spv_ui_frag));

    litPipe_ = buildPipeline(litV, litF, mainPass_, sceneLayout_, true, true, true, false, VK_CULL_MODE_BACK_BIT, false, false);
    shadowPipe_ = buildPipeline(shV, VK_NULL_HANDLE, shadowPass_, sceneLayout_, true, true, true, false, VK_CULL_MODE_NONE, true, false);
    waterPipe_ = buildPipeline(waV, waF, mainPass_, sceneLayout_, true, true, false, true, VK_CULL_MODE_NONE, false, false);
    skyPipe_ = buildPipeline(skV, skF, mainPass_, sceneLayout_, false, false, false, false, VK_CULL_MODE_NONE, false, false);
    uiPipe_ = buildPipeline(uiV, uiF, mainPass_, uiLayout_, false, false, false, true, VK_CULL_MODE_NONE, false, true);

    for (auto m : {litV, litF, shV, waV, waF, skV, skF, uiV, uiF}) vkDestroyShaderModule(device_, m, nullptr);
    return litPipe_ && shadowPipe_ && waterPipe_ && skyPipe_ && uiPipe_;
}

bool Renderer::createFrames() {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(gpu_, shadowFormat_, &fp);
    bool linear = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    si.maxLod = 1.0f;
    VK_CHECK(vkCreateSampler(device_, &si, nullptr, &shadowSampler_));

    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, FRAMES}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FRAMES}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = FRAMES;
    dp.poolSizeCount = 2;
    dp.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(device_, &dp, nullptr, &descPool_));

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
        f.ui = createBuffer(sizeof(UIQuad) * MAX_UI, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostVis);
        f.shadow = createImage(SHADOW_SIZE, SHADOW_SIZE, shadowFormat_,
                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = shadowPass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &f.shadow.view;
        fi.width = fi.height = SHADOW_SIZE;
        fi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device_, &fi, nullptr, &f.shadowFb));

        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = descPool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &setLayout_;
        VK_CHECK(vkAllocateDescriptorSets(device_, &dai, &f.set));
        VkDescriptorBufferInfo bi{f.ubo.buf, 0, sizeof(SceneUBO)};
        VkDescriptorImageInfo ii{shadowSampler_, f.shadow.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[2]{};
        w[0].sType = w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = w[1].dstSet = f.set;
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].pBufferInfo = &bi;
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].pImageInfo = &ii;
        vkUpdateDescriptorSets(device_, 2, w, 0, nullptr);
    }
    return true;
}

void Renderer::createMeshes() {
    for (int m = 0; m < MESH_COUNT; m++) {
        std::vector<Vertex> v;
        std::vector<uint32_t> i;
        switch (m) {
            case MESH_CUBE: buildCube(v, i); break;
            case MESH_SPHERE: buildSphere(v, i, 10, 14); break;
            case MESH_CONE: buildCone(v, i, 10); break;
            case MESH_CYL: buildCylinder(v, i, 10); break;
        }
        meshes_[m] = uploadMesh(v, i);
    }
    // Ocean: a big grid, displaced into waves in the vertex shader.
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    const int N = 180;
    const float S = 3000.0f;
    for (int z = 0; z <= N; z++)
        for (int x = 0; x <= N; x++)
            v.push_back({{-S / 2 + S * x / N, 0, -S / 2 + S * z / N}, {0, 1, 0}, {1, 1, 1}});
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            uint32_t a = z * (N + 1) + x, b = a + N + 1;
            idx.insert(idx.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    water_ = uploadMesh(v, idx);
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
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) depthFormat_ = shadowFormat_ = VK_FORMAT_D16_UNORM;

    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = queueFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &cp, nullptr, &cmdPool_));

    if (!createSwapchain()) return false;
    if (!createRenderPasses()) return false;
    destroySwapchain();  // recreate now that the render pass exists (for framebuffers)
    if (!createSwapchain()) return false;
    if (!createPipelines()) return false;
    if (!createFrames()) return false;
    createMeshes();
    std::printf("Renderer ready on %s (%ux%u)\n", deviceName_, extent_.width, extent_.height);
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

void Renderer::shutdown() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    for (auto& f : frames_) {
        destroyBuffer(f.ubo);
        destroyBuffer(f.instances);
        destroyBuffer(f.ui);
        vkDestroyFramebuffer(device_, f.shadowFb, nullptr);
        destroyImage(f.shadow);
        vkDestroySemaphore(device_, f.imageAvailable, nullptr);
        vkDestroyFence(device_, f.fence, nullptr);
    }
    for (auto& m : meshes_) destroyMesh(m);
    destroyMesh(water_);
    if (hasTerrain_) destroyMesh(terrain_);
    destroySwapchain();
    for (auto p : {litPipe_, shadowPipe_, waterPipe_, skyPipe_, uiPipe_}) vkDestroyPipeline(device_, p, nullptr);
    vkDestroyPipelineLayout(device_, sceneLayout_, nullptr);
    vkDestroyPipelineLayout(device_, uiLayout_, nullptr);
    vkDestroyDescriptorPool(device_, descPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    vkDestroySampler(device_, shadowSampler_, nullptr);
    vkDestroyRenderPass(device_, mainPass_, nullptr);
    vkDestroyRenderPass(device_, shadowPass_, nullptr);
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
void Renderer::recordFrame(Frame& f, uint32_t imageIndex, FrameScene& scene, uint32_t counts[MESH_COUNT],
                           uint32_t firsts[MESH_COUNT], uint32_t uiCount) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);
    VkDeviceSize zero = 0;

    auto drawScene = [&](VkPipeline pipe) {
        vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneLayout_, 0, 1, &f.set, 0, nullptr);
        if (hasTerrain_ && scene.drawTerrain) {
            VkBuffer bufs[2] = {terrain_.vb.buf, f.instances.buf};
            VkDeviceSize offs[2] = {0, 0};
            vkCmdBindVertexBuffers(f.cmd, 0, 2, bufs, offs);
            vkCmdBindIndexBuffer(f.cmd, terrain_.ib.buf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(f.cmd, terrain_.indexCount, 1, 0, 0, 0);
        }
        for (int m = 0; m < MESH_COUNT; m++) {
            if (!counts[m]) continue;
            VkBuffer bufs[2] = {meshes_[m].vb.buf, f.instances.buf};
            VkDeviceSize offs[2] = {0, 0};
            vkCmdBindVertexBuffers(f.cmd, 0, 2, bufs, offs);
            vkCmdBindIndexBuffer(f.cmd, meshes_[m].ib.buf, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(f.cmd, meshes_[m].indexCount, counts[m], 0, 0, firsts[m]);
        }
    };

    // Shadow map pass.
    {
        VkClearValue clear{};
        clear.depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = shadowPass_;
        rb.framebuffer = f.shadowFb;
        rb.renderArea = {{0, 0}, {SHADOW_SIZE, SHADOW_SIZE}};
        rb.clearValueCount = 1;
        rb.pClearValues = &clear;
        vkCmdBeginRenderPass(f.cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)SHADOW_SIZE, (float)SHADOW_SIZE, 0, 1};
        VkRect2D sc{{0, 0}, {SHADOW_SIZE, SHADOW_SIZE}};
        vkCmdSetViewport(f.cmd, 0, 1, &vp);
        vkCmdSetScissor(f.cmd, 0, 1, &sc);
        if (scene.ubo.sunDir.w > 0.01f) drawScene(shadowPipe_);
        vkCmdEndRenderPass(f.cmd);
    }

    // Main pass.
    VkClearValue clears[2]{};
    clears[0].color = {{0.02f, 0.02f, 0.03f, 1}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = mainPass_;
    rb.framebuffer = framebuffers_[imageIndex];
    rb.renderArea = {{0, 0}, extent_};
    rb.clearValueCount = 2;
    rb.pClearValues = clears;
    vkCmdBeginRenderPass(f.cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)extent_.width, (float)extent_.height, 0, 1};
    VkRect2D sc{{0, 0}, extent_};
    vkCmdSetViewport(f.cmd, 0, 1, &vp);
    vkCmdSetScissor(f.cmd, 0, 1, &sc);

    vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipe_);
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneLayout_, 0, 1, &f.set, 0, nullptr);
    vkCmdDraw(f.cmd, 3, 1, 0, 0);

    drawScene(litPipe_);

    if (scene.drawWater) {
        vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipe_);
        vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sceneLayout_, 0, 1, &f.set, 0, nullptr);
        VkBuffer bufs[2] = {water_.vb.buf, f.instances.buf};
        VkDeviceSize offs[2] = {0, 0};
        vkCmdBindVertexBuffers(f.cmd, 0, 2, bufs, offs);
        vkCmdBindIndexBuffer(f.cmd, water_.ib.buf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(f.cmd, water_.indexCount, 1, 0, 0, 0);
    }

    if (uiCount) {
        vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipe_);
        float screen[4] = {(float)extent_.width, (float)extent_.height, 0, 0};
        vkCmdPushConstants(f.cmd, uiLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, screen);
        vkCmdBindVertexBuffers(f.cmd, 0, 1, &f.ui.buf, &zero);
        vkCmdDraw(f.cmd, 6, uiCount, 0, 0);
    }
    vkCmdEndRenderPass(f.cmd);
    vkEndCommandBuffer(f.cmd);
}

void Renderer::render(FrameScene& scene) {
    if (swapchainDirty_) {
        vkDeviceWaitIdle(device_);
        destroySwapchain();
        createSwapchain();
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

    // Upload per-frame data.
    std::memcpy(f.ubo.mapped, &scene.ubo, sizeof(SceneUBO));
    Instance* dst = (Instance*)f.instances.mapped;
    dst[0] = {mat4::identity(), {1, 1, 1, 0}};  // terrain / water instance
    uint32_t cursor = 1, counts[MESH_COUNT], firsts[MESH_COUNT];
    for (int m = 0; m < MESH_COUNT; m++) {
        uint32_t n = (uint32_t)std::min<size_t>(scene.inst[m].size(), MAX_INSTANCES - cursor);
        firsts[m] = cursor;
        counts[m] = n;
        if (n) std::memcpy(dst + cursor, scene.inst[m].data(), n * sizeof(Instance));
        cursor += n;
    }
    uint32_t uiCount = (uint32_t)std::min<size_t>(scene.ui.size(), MAX_UI);
    if (uiCount) std::memcpy(f.ui.mapped, scene.ui.data(), uiCount * sizeof(UIQuad));

    vkResetCommandBuffer(f.cmd, 0);
    recordFrame(f, imageIndex, scene, counts, firsts, uiCount);

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

// Copies the just-rendered swapchain image to disk as a PPM (F12 / --capture).
void Renderer::saveScreenshot(uint32_t imageIndex) {
    std::string path = screenshotPath_;
    screenshotPath_.clear();
    vkQueueWaitIdle(queue_);
    VkDeviceSize size = (VkDeviceSize)extent_.width * extent_.height * 4;
    Buffer buf = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
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
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);

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
