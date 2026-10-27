#include "App.hpp"

#include "Core/Log.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <memory>
#include <optional>
#include <string_view>

namespace {

std::optional<Gpu::Type::Target::Any> surfaceTarget(GLFWwindow* window)
{
    switch (glfwGetPlatform()) {
#ifdef GLFW_EXPOSE_NATIVE_WAYLAND
    case GLFW_PLATFORM_WAYLAND:
        return Gpu::Type::Target::Wayland{ glfwGetWaylandDisplay(), glfwGetWaylandWindow(window) };
#endif
#ifdef GLFW_EXPOSE_NATIVE_X11
    case GLFW_PLATFORM_X11:
        return Gpu::Type::Target::Xlib{ glfwGetX11Display(), glfwGetX11Window(window) };
#endif
    default:
        return std::nullopt;
    }
}

/** Reads `--gpu=low-power` or `--gpu=high-performance`. */
Gpu::Type::PowerPreference parsePowerPreference(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--gpu=low-power") {
            return Gpu::Type::PowerPreference::LowPower;
        }
        if (arg == "--gpu=high-performance") {
            return Gpu::Type::PowerPreference::HighPerformance;
        }
    }
    return Gpu::Type::PowerPreference::Default;
}

} // namespace

int main(int argc, char** argv)
{
    if (!glfwInit()) {
        Core::Log::error("Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Trade Viewer", nullptr, nullptr);
    if (!window) {
        Core::Log::error("Failed to create a window");
        glfwTerminate();
        return 1;
    }

    const auto target = surfaceTarget(window);
    if (!target) {
        Core::Log::error("Unsupported windowing platform");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    std::unique_ptr<Viewer::App> app;
    bool failed = false;
    Viewer::App::create(*target, parsePowerPreference(argc, argv), [&](Viewer::App::Result result) {
        if (!result) {
            Core::Log::error("{}", result.error());
            failed = true;
            return;
        }
        app = std::move(*result);
        const auto& info = app->info();
        Core::Log::info("Using {} · {} · {}", info.device, info.backend, info.adapter);
    });

    // Set when the window system asks for a repaint, e.g. after the window
    // was uncovered without a compositor keeping its contents.
    bool repaint = false;
    glfwSetWindowUserPointer(window, &repaint);
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* refreshed) {
        *static_cast<bool*>(glfwGetWindowUserPointer(refreshed)) = true;
    });

    while (!failed && !glfwWindowShouldClose(window)) {
        // Sleep until the next event (input, resize, close) when there is
        // nothing to draw; keep going while frames are needed.
        if (app && !app->needsRender() && !repaint) {
            glfwWaitEvents();
        } else {
            glfwPollEvents();
        }
        if (!app) {
            continue;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        app->resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        app->update(glfwGetTime() * 1000.0);
        if (app->needsRender() || repaint) {
            app->render();
            repaint = false;
        }
    }

    app.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return failed ? 1 : 0;
}
