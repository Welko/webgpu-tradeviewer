#include "App.hpp"

#include "Core/Data/TradeParser.hpp"
#include "Core/Log.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <zlib.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

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

    // Set when the window system asks for a repaint, e.g. after the window
    // was uncovered without a compositor keeping its contents.
    bool repaint = false;
    glfwSetWindowUserPointer(window, &repaint);
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* refreshed) {
        *static_cast<bool*>(glfwGetWindowUserPointer(refreshed)) = true;
    });

    while (!glfwWindowShouldClose(window)) {
        // Sleep until the next event when there's nothing to draw.
        // GPU results that arrive meanwhile wait for that event.
        if (!app->needsRender() && !repaint) {
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
        if (app->needsRender() || repaint) {
            app->render();
            repaint = false;
        }
    }

    app.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
