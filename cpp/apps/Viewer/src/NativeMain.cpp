#include "App.hpp"

#include "Core/Data/TradeParser.hpp"
#include "Core/Log.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <zlib.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

// Zooming per step of the mouse wheel.
constexpr double ZoomPerWheelStep = 1.25;

/** What the window callbacks need. */
struct WindowState
{
    Viewer::App* app;
    bool repaint = false;  // the window system asked for a repaint
    bool dragging = false;
    double lastX = 0.0;  // window coordinates
    Gpu::Market::Type::MarkerScale markerScale = Gpu::Market::Type::MarkerScale::Area;
};

WindowState& stateOf(GLFWwindow* window)
{
    return *static_cast<WindowState*>(glfwGetWindowUserPointer(window));
}

/** Window coordinates in physical pixels, which differ on scaled screens. */
double toPixels(GLFWwindow* window, double x)
{
    int windowWidth = 0;
    int pixelWidth = 0;
    glfwGetWindowSize(window, &windowWidth, nullptr);
    glfwGetFramebufferSize(window, &pixelWidth, nullptr);
    return windowWidth > 0 ? x * pixelWidth / windowWidth : x;
}

/** Forwards the window's input to the app. */
void connectInput(GLFWwindow* window)
{
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* refreshed) { stateOf(refreshed).repaint = true; });

    glfwSetScrollCallback(window, [](GLFWwindow* scrolled, double, double steps) {
        double x = 0.0;
        glfwGetCursorPos(scrolled, &x, nullptr);
        // Up zooms in.
        stateOf(scrolled).app->zoomAt(toPixels(scrolled, x), std::pow(ZoomPerWheelStep, -steps));
    });

    glfwSetMouseButtonCallback(window, [](GLFWwindow* clicked, int button, int action, int) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            auto& state = stateOf(clicked);
            state.dragging = action == GLFW_PRESS;
            glfwGetCursorPos(clicked, &state.lastX, nullptr);
        }
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow* moved, double x, double) {
        auto& state = stateOf(moved);
        if (state.dragging) {
            state.app->panBy(toPixels(moved, x - state.lastX));
            state.lastX = x;
        }
    });

    glfwSetKeyCallback(window, [](GLFWwindow* pressed, int key, int, int action, int) {
        if (action != GLFW_PRESS) {
            return;
        }
        auto& state = stateOf(pressed);
        switch (key) {
        case GLFW_KEY_R:
            state.app->resetView();
            break;
        case GLFW_KEY_D:
            state.app->showDay();
            break;
        case GLFW_KEY_M:
            state.markerScale = state.markerScale == Gpu::Market::Type::MarkerScale::Area ? Gpu::Market::Type::MarkerScale::Log
                                                                                          : Gpu::Market::Type::MarkerScale::Area;
            state.app->setMarkerScale(state.markerScale);
            break;
        default:
            break;
        }
    });

    glfwSetWindowContentScaleCallback(window, [](GLFWwindow* scaled, float xScale, float) {
        stateOf(scaled).app->setPixelRatio(xScale);
    });
}

std::optional<Gpu::Type::Target::Any> surfaceTarget(GLFWwindow* window)
{
    switch (glfwGetPlatform()) {
#ifdef GLFW_EXPOSE_NATIVE_WAYLAND
    case GLFW_PLATFORM_WAYLAND:
        return Gpu::Type::Target::Wayland{ .display = glfwGetWaylandDisplay(), .surface = glfwGetWaylandWindow(window) };
#endif
#ifdef GLFW_EXPOSE_NATIVE_X11
    case GLFW_PLATFORM_X11:
        return Gpu::Type::Target::Xlib{ .display = glfwGetX11Display(), .window = glfwGetX11Window(window) };
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

/** The first argument that isn't an option: a trades file to show. */
const char* tradesPath(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (!std::string_view(argv[i]).starts_with("--")) {
            return argv[i];
        }
    }
    return nullptr;
}

/** Reads a trades CSV, gzip-compressed or not. */
std::expected<Core::Data::Trades, std::string> readTrades(const char* path)
{
    const std::unique_ptr<gzFile_s, decltype(&gzclose)> file(gzopen(path, "rb"), &gzclose);
    if (!file) {
        return std::unexpected(std::format("Can't open {}", path));
    }
    Core::Data::TradeParser parser;
    std::array<char, 64 * 1024> buffer{};
    while (true) {
        const int read = gzread(file.get(), buffer.data(), static_cast<unsigned>(buffer.size()));
        if (read < 0) {
            int code = 0;
            return std::unexpected(std::format("Can't read {}: {}", path, gzerror(file.get(), &code)));
        }
        if (read == 0) {
            return parser.finish();
        }
        if (auto appended = parser.append({ buffer.data(), static_cast<std::size_t>(read) }); !appended) {
            return std::unexpected(std::format("{}: {}", path, appended.error()));
        }
    }
}

/** A UTC time to the millisecond, such as 2019-12-01 00:00:03.572. */
std::string utc(std::int64_t timeUs)
{
    return std::format("{:%F %T}", std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(timeUs / 1000)));
}

} // namespace

int main(int argc, char** argv)
{
    std::optional<Core::Data::Trades> loaded;
    if (const char* path = tradesPath(argc, argv)) {
        auto trades = readTrades(path);
        if (!trades) {
            Core::Log::error("{}", trades.error());
            return 1;
        }
        loaded = std::move(*trades);
    }

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

    // Natively, creation finishes before create() returns.
    std::unique_ptr<Viewer::App> app;
    Viewer::App::create(*target, parsePowerPreference(argc, argv), [&](Viewer::App::Result result) {
        if (!result) {
            Core::Log::error("{}", result.error());
            return;
        }
        app = std::move(*result);
        const auto& info = app->info();
        Core::Log::info("Using {} · {} · {}", info.device, info.backend, info.adapter);
    });
    if (!app) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    app->setTrades(loaded ? std::move(*loaded) : Viewer::syntheticDay());
    const auto summary = Core::Data::summarize(app->trades());
    Core::Log::info("{} {} trades from {} to {} UTC, {:.6f} in total", summary.count, loaded ? "loaded" : "synthetic",
                    utc(summary.firstUs), utc(summary.lastUs), summary.totalAmount);

    WindowState state{ .app = app.get() };
    glfwSetWindowUserPointer(window, &state);
    connectInput(window);
    float contentScale = 1.0f;
    glfwGetWindowContentScale(window, &contentScale, nullptr);
    app->setPixelRatio(contentScale);
    Core::Log::info("Scroll to zoom, drag to pan; R: 12:00-13:00, D: whole day, M: marker size by area or log");

    while (!glfwWindowShouldClose(window)) {
        // Sleep until the next event when there's nothing to draw.
        // GPU results that arrive meanwhile wait for that event.
        if (!app->needsRender() && !state.repaint) {
            glfwWaitEvents();
        } else {
            glfwPollEvents();
        }
        app->processEvents();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        app->resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        app->update(glfwGetTime() * 1000.0);
        if (app->needsRender() || state.repaint) {
            app->render();
            state.repaint = false;
        }
    }

    app.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
