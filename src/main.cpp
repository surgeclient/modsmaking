// MYTHBOUND - a mythical-creature survival game on a custom Vulkan engine.
#include "game.h"
#include "renderer.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static Game* g_game = nullptr;

static void scrollCallback(GLFWwindow*, double, double dy) {
    if (g_game) g_game->onScroll(dy);
}

static void printUsage() {
    std::printf(
        "mythbound [options]\n"
        "  --skip-trailer        start straight on the island\n"
        "  --windowed W H        window size (default 1600x900)\n"
        "  --fullscreen          borderless fullscreen on the primary monitor\n"
        "  --validation          enable Vulkan validation layers\n"
        "  --time T              start time of day 0..1 (0.5 = noon, 0.85 = night)\n"
        "  --pos X Z             start position\n"
        "  --look YAW PITCH      start camera angles (radians)\n"
        "  --demo                start with tools, baits and a tamed griffin\n"
        "  --trailer-at S        jump into the trailer at S seconds\n"
        "  --capture FILE SECS   save a screenshot (PPM) after SECS seconds\n"
        "  --quit-after SECS     exit automatically\n"
        "  --selftest            run the automated gameplay tests and exit\n");
}

int main(int argc, char** argv) {
    Options opt;
    int winW = 1600, winH = 900;
    bool fullscreen = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](float def) { return i + 1 < argc ? (float)std::atof(argv[++i]) : def; };
        if (a == "--skip-trailer") opt.skipTrailer = true;
        else if (a == "--validation") opt.validation = true;
        else if (a == "--demo") opt.demo = true;
        else if (a == "--ui-test" && i + 1 < argc) {
            opt.uiTest = argv[++i];
            opt.skipTrailer = true;
            opt.demo = true;
        } else if (a == "--selftest") {
            opt.selfTest = true;
            opt.skipTrailer = true;
        }
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--windowed") {
            winW = (int)next(1600);
            winH = (int)next(900);
        } else if (a == "--time") opt.startTime = next(0.3f);
        else if (a == "--pos") {
            opt.hasPos = true;
            opt.posX = next(0);
            opt.posZ = next(0);
        } else if (a == "--look") {
            opt.yaw = next(PI);
            opt.pitch = next(-0.15f);
        } else if (a == "--trailer-at") opt.trailerAt = next(0);
        else if (a == "--capture") {
            if (i + 1 < argc) opt.capturePath = argv[++i];
            opt.captureAfter = next(3);
        } else if (a == "--quit-after") opt.quitAfter = next(10);
        else if (a == "--help" || a == "-h") {
            printUsage();
            return 0;
        } else {
            std::fprintf(stderr, "Unknown option %s\n", a.c_str());
            printUsage();
            return 1;
        }
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialise GLFW\n");
        return 1;
    }
    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "Vulkan is not available. Update your graphics driver.\n");
        glfwTerminate();
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWmonitor* monitor = nullptr;
    if (fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        winW = mode->width;
        winH = mode->height;
    }
    GLFWwindow* window = glfwCreateWindow(winW, winH, "MYTHBOUND", monitor, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    Renderer renderer;
    if (!renderer.init(window, opt.validation)) {
        std::fprintf(stderr, "Failed to initialise Vulkan renderer\n");
        renderer.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    Game game;
    g_game = &game;
    glfwSetScrollCallback(window, scrollCallback);
    if (!game.init(window, &renderer, opt)) return 1;
    if (opt.selfTest) {
        bool ok = game.runSelfTest();
        renderer.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return ok ? 0 : 1;
    }

    auto last = std::chrono::high_resolution_clock::now();
    auto begin = last;
    double fpsTimer = 0;
    int frames = 0, totalFrames = 0;
    while (!game.wantsQuit()) {
        glfwPollEvents();
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        game.frame(dt);
        frames++;
        totalFrames++;
        fpsTimer += dt;
        if (fpsTimer >= 1.0) {
            char title[384];
            std::snprintf(title, sizeof(title), "MYTHBOUND  |  %d FPS  |  %s", frames, renderer.deviceName());
            glfwSetWindowTitle(window, title);
            frames = 0;
            fpsTimer = 0;
        }
    }

    double secs = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - begin).count();
    std::printf("Average %.1f FPS over %d frames\n", totalFrames / std::max(secs, 0.001), totalFrames);
    renderer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
