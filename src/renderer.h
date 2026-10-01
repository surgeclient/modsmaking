#pragma once
#include "vk_funcs.h"
#include "math.h"
#include <vector>
#include <string>

struct GLFWwindow;

// Mesh vertex. color.a = emissive, skin = (bone0, bone1, bone1 weight, ambient occlusion),
// sway = wind influence for foliage.
struct Vertex {
    vec3 pos;
    vec3 normal;
    vec4 color;
    vec4 skin;
    float sway;  // wind weight (static meshes) / extra gloss (skinned meshes)
    vec2 uv;     // card texture coordinates
    float card;  // 0 = solid surface, else alpha-cut card type (1 leaves, 2 needles, 3 fronds, 4 feathers)
};

// Per-instance data. color.rgb tints the mesh, color.a scales its emissive parts.
// params = (gloss, surface pattern, wind strength, first bone index for skinned meshes).
struct Instance {
    mat4 model;
    vec4 color;
    vec4 params;
};

struct ParticleInst {
    vec4 posSize;  // xyz position, w size
    vec4 color;    // rgb, a = opacity
};

struct UIQuad {
    vec4 rect;   // type 0: x, y, w, h (top-left origin). type 1: x0, y0, x1, y1 line segment
    vec4 color;  // rgba
    vec4 extra;  // x type (0 rounded rect, 1 line), y corner radius / line thickness, z edge softness
};

enum BuiltinMesh { MESH_CUBE, MESH_SPHERE, MESH_CONE, MESH_CYL, MESH_BUILTIN_COUNT };

constexpr int MAX_POINT_LIGHTS = 16;

// Layout matches shaders/scene_ubo.glsl (std140).
struct SceneUBO {
    mat4 viewProj;
    mat4 invViewProj;
    mat4 lightViewProj[2];  // shadow cascades: near, far
    vec4 camPos;
    vec4 sunDir;       // xyz towards the key light, w = shadow strength
    vec4 sunColor;
    vec4 skyColor;     // zenith / ambient from above
    vec4 groundColor;  // ambient from below
    vec4 fogColor;     // horizon colour, w = fog density
    vec4 params;       // x time, y night 0..1, z hollow glow, w point light count
    vec4 moonDir;
    vec4 skySunDir;    // real sun direction for the sky dome
    vec4 hollowPos;    // xyz centre of the Hollow, w crater radius
    vec4 camRight;
    vec4 camUp;
    vec4 playerPos;    // for grass bending
    vec4 post;         // x exposure, y bloom strength, z saturation, w vignette
    vec4 world;        // x terrain half size, y grass radius, z wind strength, w sea level
    vec4 lightPos[MAX_POINT_LIGHTS];    // w = radius
    vec4 lightColor[MAX_POINT_LIGHTS];  // w = intensity
};

struct FrameScene {
    SceneUBO ubo{};
    std::vector<std::vector<Instance>> inst;     // static meshes, indexed by mesh id
    std::vector<std::vector<Instance>> instFar;  // static meshes that skip the shadow pass
    std::vector<std::vector<Instance>> skinned;  // skinned meshes, indexed by mesh id
    std::vector<mat4> bones;
    std::vector<ParticleInst> particlesAdd, particlesAlpha;
    std::vector<UIQuad> ui;
    vec4 post2;  // x,y sun position on screen (uv), z god-ray strength, w ambient-occlusion strength
    vec4 post3;  // x tan(fovY/2), y aspect, z time, w sharpening
    vec4 sunScreenColor;
    bool drawTerrain = true, drawWater = true, drawGrass = true;
    void reset(int meshCount) {
        inst.assign(meshCount, {});
        instFar.assign(meshCount, {});
        skinned.assign(meshCount, {});
        bones.clear();
        particlesAdd.clear();
        particlesAlpha.clear();
        ui.clear();
    }
};

class Renderer {
public:
    bool init(GLFWwindow* window, bool validation);
    void shutdown();
    int addMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx);
    int meshCount() const { return (int)meshes_.size(); }
    void uploadTerrain(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx);
    // n x n height samples (metres) and grass colour/density covering [-half, half]^2.
    void uploadTerrainMaps(int n, const std::vector<float>& heights, const std::vector<uint32_t>& grassRGBA);
    void render(FrameScene& scene);
    void requestScreenshot(const std::string& path) { screenshotPath_ = path; }
    int width() const { return (int)extent_.width; }
    int height() const { return (int)extent_.height; }
    const char* deviceName() const { return deviceName_; }
    int msaaSamples() const { return (int)samples_; }

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
        uint32_t w = 0, h = 0;
    };
    struct MeshGPU {
        Buffer vb, ib;
        uint32_t indexCount = 0;
    };
    static constexpr int FRAMES = 2;
    static constexpr int BLOOM_LEVELS = 5;
    static constexpr uint32_t SHADOW_W = 4096, SHADOW_H = 2048;
    static constexpr uint32_t MAX_INSTANCES = 120000;
    static constexpr uint32_t MAX_BONES = 32768;
    static constexpr uint32_t MAX_PARTICLES = 20000;
    static constexpr uint32_t MAX_UI = 60000;
    static constexpr int GRASS_N = 300;  // grass grid is GRASS_N x GRASS_N blades around the camera

    struct Frame {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        Buffer ubo, instances, bones, particles, ui;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    struct PipeCfg {
        VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
        VkRenderPass pass = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        int input = 0;  // 0 none, 1 mesh+instance, 2 mesh vertices only, 3 particles, 4 ui
        bool depthTest = false, depthWrite = false;
        int blend = 0;  // 0 opaque, 1 alpha, 2 additive
        VkCullModeFlags cull = VK_CULL_MODE_NONE;
        bool depthBias = false;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        bool colorOutput = true;
        bool rgbOnly = false;  // keep alpha (it stores scene depth) untouched
    };

    bool createInstance(bool validation);
    bool pickDevice();
    bool createSwapchain();
    void destroySwapchain();
    bool createTargets();
    void destroyTargets();
    bool createRenderPasses();
    bool createPipelines();
    bool createStatic();
    bool createFrames();
    void createBuiltinMeshes();
    void writePostSets();
    void writeSceneSets();
    uint32_t findMemory(uint32_t typeBits, VkMemoryPropertyFlags props);
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
    void destroyBuffer(Buffer& b);
    Buffer createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    Image createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                      VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    void destroyImage(Image& i);
    Image uploadImage(uint32_t w, uint32_t h, VkFormat fmt, const void* data, size_t bytes);
    VkCommandBuffer beginOneShot();
    void endOneShot(VkCommandBuffer cmd);
    MeshGPU uploadMesh(const std::vector<Vertex>& v, const std::vector<uint32_t>& idx);
    void destroyMesh(MeshGPU& m);
    VkShaderModule shaderModule(const uint32_t* code, size_t bytes);
    VkPipeline buildPipeline(const PipeCfg& c);
    void recordFrame(Frame& f, uint32_t imageIndex, FrameScene& scene);
    void saveScreenshot(uint32_t imageIndex);
    void drawMeshes(VkCommandBuffer cmd, Frame& f, FrameScene& scene, VkPipeline staticPipe, VkPipeline skinPipe, VkPipeline terrainPipe, bool far);

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
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D extent_{};
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    std::vector<VkFramebuffer> swapFbs_;
    std::vector<VkSemaphore> renderFinished_;
    bool swapchainDirty_ = false;

    // Offscreen targets.
    const VkFormat hdrFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    Image msaaColor_, msaaDepth_, hdr_, bloom_[BLOOM_LEVELS], ao_, rays_;
    VkFramebuffer mainFb_ = VK_NULL_HANDLE, bloomFb_[BLOOM_LEVELS] = {}, aoFb_ = VK_NULL_HANDLE, raysFb_ = VK_NULL_HANDLE;
    Image shadow_;
    VkFramebuffer shadowFb_ = VK_NULL_HANDLE;
    Image heightTex_, grassTex_;

    VkRenderPass shadowPass_ = VK_NULL_HANDLE, mainPass_ = VK_NULL_HANDLE;
    VkRenderPass bloomPass_ = VK_NULL_HANDLE, bloomLoadPass_ = VK_NULL_HANDLE, finalPass_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout sceneSetLayout_ = VK_NULL_HANDLE, postSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet downSets_[BLOOM_LEVELS] = {}, upSets_[BLOOM_LEVELS] = {}, compositeSet_ = VK_NULL_HANDLE, aoSet_ = VK_NULL_HANDLE, raysSet_ = VK_NULL_HANDLE;
    VkPipelineLayout sceneLayout_ = VK_NULL_HANDLE, postLayout_ = VK_NULL_HANDLE, uiLayout_ = VK_NULL_HANDLE;
    VkSampler shadowSampler_ = VK_NULL_HANDLE, linearSampler_ = VK_NULL_HANDLE;

    VkPipeline shadowPipe_ = VK_NULL_HANDLE, shadowSkinPipe_ = VK_NULL_HANDLE;
    VkPipeline terrainPipe_ = VK_NULL_HANDLE, litPipe_ = VK_NULL_HANDLE, skinPipe_ = VK_NULL_HANDLE;
    VkPipeline grassPipe_ = VK_NULL_HANDLE, waterPipe_ = VK_NULL_HANDLE, skyPipe_ = VK_NULL_HANDLE;
    VkPipeline particleAddPipe_ = VK_NULL_HANDLE, particleAlphaPipe_ = VK_NULL_HANDLE;
    VkPipeline ssaoPipe_ = VK_NULL_HANDLE, raysPipe_ = VK_NULL_HANDLE;
    VkPipeline bloomDownPipe_ = VK_NULL_HANDLE, bloomUpPipe_ = VK_NULL_HANDLE, compositePipe_ = VK_NULL_HANDLE, uiPipe_ = VK_NULL_HANDLE;

    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    Frame frames_[FRAMES];
    int frameIndex_ = 0;

    std::vector<MeshGPU> meshes_;
    MeshGPU terrain_, water_, grassBlade_;
    bool hasTerrain_ = false;

    std::string screenshotPath_;
};

// Primitive mesh builders (unit sized, centred; cone/cylinder run along +Y from -0.5 to 0.5).
void buildCube(std::vector<Vertex>& v, std::vector<uint32_t>& i);
void buildSphere(std::vector<Vertex>& v, std::vector<uint32_t>& i, int rings, int segs);
void buildCone(std::vector<Vertex>& v, std::vector<uint32_t>& i, int segs);
void buildCylinder(std::vector<Vertex>& v, std::vector<uint32_t>& i, int segs);
