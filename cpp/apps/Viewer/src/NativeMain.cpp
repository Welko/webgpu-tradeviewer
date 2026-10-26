#include "App.hpp"

#include "Core/Log.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <memory>
#include <optional>
#include <string_view>

namespace {

std::optional<Graphics::Type::Target::Any> surfaceTarget(GLFWwindow* window)
{
    switch (glfwGetPlatform()) {
#ifdef GLFW_EXPOSE_NATIVE_WAYLAND
    case GLFW_PLATFORM_WAYLAND:
        return Graphics::Type::Target::Wayland{ glfwGetWaylandDisplay(), glfwGetWaylandWindow(window) };
#endif
#ifdef GLFW_EXPOSE_NATIVE_X11
    case GLFW_PLATFORM_X11:
        return Graphics::Type::Target::Xlib{ glfwGetX11Display(), glfwGetX11Window(window) };
#endif
    default:
        return std::nullopt;
    }
}

/** Reads `--gpu=low-power` or `--gpu=high-performance`. */
Graphics::Type::PowerPreference powerPreference(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--gpu=low-power") {
            return Graphics::Type::PowerPreference::LowPower;
        }
        if (arg == "--gpu=high-performance") {
            return Graphics::Type::PowerPreference::HighPerformance;
        }
    }
    return Graphics::Type::PowerPreference::Default;
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
    Viewer::App::create(*target, powerPreference(argc, argv), [&](Viewer::App::Result result) {
        if (!result) {
            Core::Log::error("{}", result.error());
            failed = true;
            return;
        }
        app = std::move(*result);
        Core::Log::info("Using {}", app->describe());
    });

    while (!failed && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (!app) {
            continue;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        app->resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        app->update(glfwGetTime() * 1000.0);
        app->render();
    }

    app.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return failed ? 1 : 0;
}
