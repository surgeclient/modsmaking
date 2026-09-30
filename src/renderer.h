#pragma once
#include "vk_funcs.h"
#include "math.h"
#include <vector>
#include <string>

struct GLFWwindow;

struct Vertex {
    vec3 pos;
    vec3 normal;
    vec3 color;
};

// One instanced draw of a primitive: model matrix + tint (w = emissive 0..1+).
struct Instance {
    mat4 model;
    vec4 color;
};

struct UIQuad {
    vec4 rect;   // x, y, w, h in pixels (top-left origin)
    vec4 color;  // rgba
};

enum MeshId { MESH_CUBE, MESH_SPHERE, MESH_CONE, MESH_CYL, MESH_COUNT };

constexpr int MAX_POINT_LIGHTS = 16;

// Layout matches the std140 uniform block in every shader.
struct SceneUBO {
    mat4 viewProj;
    mat4 invViewProj;
    mat4 lightViewProj;
    vec4 camPos;
    vec4 sunDir;       // xyz = direction towards the light, w = shadow strength
    vec4 sunColor;
    vec4 skyColor;     // ambient from above
    vec4 groundColor;  // ambient from below
    vec4 fogColor;     // w = fog density
    vec4 params;       // x = time, y = night factor 0..1, z = hollow glow, w = light count
    vec4 moonDir;
    vec4 skySunDir;    // real sun direction for the sky dome
    vec4 hollowPos;    // xyz = centre of the Hollow, w = radius
    vec4 lightPos[MAX_POINT_LIGHTS];    // w = radius
    vec4 lightColor[MAX_POINT_LIGHTS];  // w = intensity
};

struct FrameScene {
    SceneUBO ubo{};
    std::vector<Instance> inst[MESH_COUNT];
    std::vector<UIQuad> ui;
    bool drawTerrain = true;
    bool drawWater = true;
    void clear() {
        for (auto& v : inst) v.clear();
        ui.clear();
    }
};

class Renderer {
public:
    bool init(GLFWwindow* window, bool validation);
    void shutdown();
    void uploadTerrain(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx);
    void render(FrameScene& scene);
    void requestScreenshot(const std::string& path) { screenshotPath_ = path; }
    int width() const { return (int)extent_.width; }
    int height() const { return (int)extent_.height; }
    const char* deviceName() const { return deviceName_; }

private:
    struct Buffer {
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };
    struct Image {
        VkImage img = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct MeshGPU {
        Buffer vb, ib;
        uint32_t indexCount = 0;
    };
    static constexpr int FRAMES = 2;
    static constexpr uint32_t SHADOW_SIZE = 2048;
    static constexpr uint32_t MAX_INSTANCES = 160000;
    static constexpr uint32_t MAX_UI = 60000;

    struct Frame {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Buffer ubo, instances, ui;
        Image shadow;
        VkFramebuffer shadowFb = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    bool createInstance(bool validation);
    bool pickDevice();
    bool createSwapchain();
    void destroySwapchain();
    bool createRenderPasses();
    bool createPipelines();
    bool createFrames();
    void createMeshes();
    uint32_t findMemory(uint32_t typeBits, VkMemoryPropertyFlags props);
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
    void destroyBuffer(Buffer& b);
    Buffer createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    Image createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    void destroyImage(Image& i);
    MeshGPU uploadMesh(const std::vector<Vertex>& v, const std::vector<uint32_t>& idx);
    void destroyMesh(MeshGPU& m);
    VkShaderModule shaderModule(const uint32_t* code, size_t bytes);
    VkPipeline buildPipeline(VkShaderModule vs, VkShaderModule fs, VkRenderPass pass, VkPipelineLayout layout,
                             bool meshInput, bool depthTest, bool depthWrite, bool blend, VkCullModeFlags cull,
                             bool depthBias, bool uiInput);
    void recordFrame(Frame& f, uint32_t imageIndex, FrameScene& scene, uint32_t counts[MESH_COUNT], uint32_t firsts[MESH_COUNT], uint32_t uiCount);
    void saveScreenshot(uint32_t imageIndex);

    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkPhysicalDeviceMemoryProperties memProps_{};
    char deviceName_[256] = {};

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D extent_{};
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> renderFinished_;  // one per swapchain image
    Image depth_;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkFormat shadowFormat_ = VK_FORMAT_D32_SFLOAT;
    bool swapchainDirty_ = false;

    VkRenderPass mainPass_ = VK_NULL_HANDLE;
    VkRenderPass shadowPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkPipelineLayout sceneLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout uiLayout_ = VK_NULL_HANDLE;
    VkPipeline litPipe_ = VK_NULL_HANDLE, shadowPipe_ = VK_NULL_HANDLE, waterPipe_ = VK_NULL_HANDLE;
    VkPipeline skyPipe_ = VK_NULL_HANDLE, uiPipe_ = VK_NULL_HANDLE;
    VkSampler shadowSampler_ = VK_NULL_HANDLE;

    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    Frame frames_[FRAMES];
    int frameIndex_ = 0;

    MeshGPU meshes_[MESH_COUNT];
    MeshGPU terrain_, water_;
    bool hasTerrain_ = false;

    std::string screenshotPath_;
};

// Primitive mesh builders (unit sized, centred; cone/cylinder run along +Y from -0.5 to 0.5).
void buildCube(std::vector<Vertex>& v, std::vector<uint32_t>& i);
void buildSphere(std::vector<Vertex>& v, std::vector<uint32_t>& i, int rings, int segs);
void buildCone(std::vector<Vertex>& v, std::vector<uint32_t>& i, int segs);
void buildCylinder(std::vector<Vertex>& v, std::vector<uint32_t>& i, int segs);
