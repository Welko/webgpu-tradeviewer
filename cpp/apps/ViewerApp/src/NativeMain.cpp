#include "Png.hpp"
#include "Viewer/App.hpp"
#include "Viewer/Scene.hpp"

#include "Core/Data/TradeFile.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <nlohmann/json.hpp>

#include <sys/utsname.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// Zooming per step of the mouse wheel.
constexpr double ZoomPerWheelStep = 1.25;

/** What the window callbacks need. */
struct WindowState
{
    Viewer::App* app;
    bool repaint = false;  // the window system asked for a repaint
    bool dragging = false;
    double lastXCssPx = 0.0;  // window coordinates
    Gpu::Market::Type::MarkerScale markerScale = Gpu::Market::Type::MarkerScale::Area;
    bool summariesAllowed = true;
};

WindowState& getState(GLFWwindow* window)
{
    return *static_cast<WindowState*>(glfwGetWindowUserPointer(window));
}

/** Logs what marker sizes mean, as the web app's legend shows it, since the window has no text. */
void logLegend(const Viewer::App& app, Gpu::Market::Type::MarkerScale scale)
{
    using Bound = Gpu::Market::Type::LegendSize::Bound;
    std::string sizes;
    for (const auto& size : app.getLegend().sizes) {
        const char* prefix = size.bound == Bound::AtMost ? "<= " : size.bound == Bound::AtLeast ? ">= " : "";
        sizes += std::format("{}{}{:.4g} BTC {:.3g} px", sizes.empty() ? "" : ", ", prefix, size.amount, size.radiusCssPx);
    }
    Core::Log::info("Buys green, sells red; marker radius by {}: {}", scale == Gpu::Market::Type::MarkerScale::Area ? "area" : "log", sizes);
}

/** Logs what the trades and what's derived from them hold, the GPU's share, and the whole process, to compare. */
void logMemory(const Viewer::App& app)
{
    const auto memory = app.getMemoryStats();
    const auto count = static_cast<double>(std::max<std::size_t>(1, app.getTrades().size()));
    const auto line = [count](std::string_view what, std::uint64_t bytes) {
        return std::format("\n  {:<16} {:>8.1f} MiB  {:>5.1f} B per trade", what, static_cast<double>(bytes) / 1048576.0,
                           static_cast<double>(bytes) / count);
    };
    const std::size_t derived = memory.totalsBytes + memory.priceTreeBytes + memory.summariesBytes;
    const auto process = Core::Memory::getProcessBytes();
    Core::Log::info("Memory:{}{}{}{}{}{}\n  The derived data adds {:.0f}% to the trades.{}", line("trades", memory.tradesBytes),
                    line("running totals", memory.totalsBytes), line("price tree", memory.priceTreeBytes),
                    line("column summaries", memory.summariesBytes), line("total", memory.getCpuBytes()),
                    line("on the GPU", memory.gpuBytes),
                    memory.tradesBytes > 0 ? 100.0 * static_cast<double>(derived) / static_cast<double>(memory.tradesBytes) : 0.0,
                    process ? std::format(" The process uses {:.1f} MiB in all.", static_cast<double>(*process) / 1048576.0) : "");
}

/** Window coordinates in physical pixels, which differ on scaled screens. */
double toPx(GLFWwindow* window, double cssPx)
{
    int windowWidthCssPx = 0;
    int windowWidthPx = 0;
    glfwGetWindowSize(window, &windowWidthCssPx, nullptr);
    glfwGetFramebufferSize(window, &windowWidthPx, nullptr);
    return windowWidthCssPx > 0 ? cssPx * windowWidthPx / windowWidthCssPx : cssPx;
}

/** Forwards the window's input to the app. */
void connectInput(GLFWwindow* window)
{
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* refreshed) { getState(refreshed).repaint = true; });

    glfwSetScrollCallback(window, [](GLFWwindow* scrolled, double, double steps) {
        double xCssPx = 0.0;
        glfwGetCursorPos(scrolled, &xCssPx, nullptr);
        // Up zooms in.
        getState(scrolled).app->zoomAt(toPx(scrolled, xCssPx), std::pow(ZoomPerWheelStep, -steps));
    });

    glfwSetMouseButtonCallback(window, [](GLFWwindow* clicked, int button, int action, int) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            auto& state = getState(clicked);
            state.dragging = action == GLFW_PRESS;
            glfwGetCursorPos(clicked, &state.lastXCssPx, nullptr);
        }
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow* moved, double xCssPx, double yCssPx) {
        auto& state = getState(moved);
        if (state.dragging) {
            state.app->panBy(toPx(moved, xCssPx - state.lastXCssPx));
            state.lastXCssPx = xCssPx;
        }
        state.app->setPointer(toPx(moved, xCssPx), toPx(moved, yCssPx));
    });

    glfwSetCursorEnterCallback(window, [](GLFWwindow* crossed, int entered) {
        if (!entered) {
            getState(crossed).app->clearPointer();
        }
    });

    glfwSetKeyCallback(window, [](GLFWwindow* pressed, int key, int, int action, int) {
        if (action != GLFW_PRESS) {
            return;
        }
        auto& state = getState(pressed);
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
            logLegend(*state.app, state.markerScale);
            break;
        case GLFW_KEY_S:
            state.summariesAllowed = !state.summariesAllowed;
            state.app->setSummariesAllowed(state.summariesAllowed);
            Core::Log::info("{}", state.summariesAllowed ? "Crowded views show column summaries" : "Every trade is drawn, however crowded");
            break;
        default:
            break;
        }
    });

    glfwSetWindowContentScaleCallback(window, [](GLFWwindow* scaled, float xScale, float) {
        getState(scaled).app->setPixelRatio(xScale);
    });
}

std::optional<Gpu::Type::Target::Any> getSurfaceTarget(GLFWwindow* window)
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

/** The value of the option starting with `prefix`, such as "--scene=". */
std::optional<std::string> getOption(int argc, char** argv, std::string_view prefix)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg.starts_with(prefix)) {
            return std::string(arg.substr(prefix.size()));
        }
    }
    return std::nullopt;
}

/** The first argument that isn't an option: a trades file to show. */
const char* getTradesPath(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (!std::string_view(argv[i]).starts_with("--")) {
            return argv[i];
        }
    }
    return nullptr;
}

/** A UTC time to the millisecond, such as 2019-12-01 00:00:03.572. */
std::string formatUtc(std::int64_t timeUs)
{
    return std::format("{:%F %T}", std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(timeUs / 1000)));
}

/**
 * The trade under the pointer, until the native UI can show it beside its
 * marker. Otherwise the view's range and what it holds.
 */
std::string getWindowTitle(const Viewer::App& app)
{
    if (const auto column = app.getPickedColumn()) {
        const auto& summary = column->summary;
        return std::format("Trade Viewer · {} to {} UTC · {} trades ({} buys), {} to {} USDT, {:.8f} BTC bought, {:.8f} sold",
                           formatUtc(column->time.startUs), formatUtc(column->time.endUs), summary.trades.size(), summary.buyCount,
                           summary.price.low, summary.price.high, summary.buyAmount, summary.sellAmount);
    }
    if (const auto picked = app.getPickedTrade()) {
        const auto ms = picked->timeUs / 1000 - (picked->timeUs % 1000 < 0 ? 1 : 0);
        return std::format("Trade Viewer · {} {} BTC at {} USDT · {}{:03} UTC", picked->side == Core::Data::Side::Buy ? "Buy" : "Sell",
                           picked->amount, picked->price, formatUtc(ms * 1000), picked->timeUs - ms * 1000);
    }
    const auto view = app.getView();
    const auto stats = app.getViewStats();
    return std::format("Trade Viewer · {} to {} UTC · {} trades, {:.8f} BTC{}", formatUtc(view.startUs), formatUtc(view.endUs),
                       stats.count, stats.totalAmount,
                       app.getColumnsAcross() > 0 ? " · summarized: one bar per pixel column" : "");
}

/**
 * `scene` and its known pixels as JSON, written beside its snapshot for
 * tools/compare-renderings.mjs to check it with, as web/tests/scenes.spec.ts
 * writes it for the browsers.
 */
nlohmann::ordered_json getSceneJson(const Viewer::Scene& scene, const std::vector<Viewer::Scene::Patch>& patches)
{
    auto list = nlohmann::ordered_json::array();
    for (const auto& patch : patches) {
        list.push_back({ { "what", patch.what },
                         { "xPx", patch.xPx },
                         { "yPx", patch.yPx },
                         { "widthPx", patch.widthPx },
                         { "heightPx", patch.heightPx },
                         { "rgba", patch.rgba },
                         { "tolerance", patch.tolerance } });
    }
    return { { "name", scene.name },
             { "description", scene.description },
             { "widthPx", scene.widthPx },
             { "heightPx", scene.heightPx },
             { "patches", std::move(list) } };
}

/** What the comparison report says about this platform, as web/tests/scenes.spec.ts writes it for the browsers. */
nlohmann::ordered_json getAboutJson(const Gpu::Type::Info& info)
{
    utsname system{};
    uname(&system);
    auto scenes = nlohmann::ordered_json::array();
    for (const auto& scene : Viewer::Scene::getScenes()) {
        scenes.push_back(scene.name);
    }
    return { { "platform", "native" },
             { "version", std::format("wgpu-native {}, {} {}", TRADEVIEWER_WGPU_VERSION, system.sysname, system.machine) },
             { "gpu", std::format("{} · {} · {}", info.device, info.backend, info.adapter) },
             { "scenes", std::move(scenes) } };
}

/** Writes `json` to `path` as the browsers' tests do: indented by 2, with a final newline. */
void writeJson(const std::filesystem::path& path, const nlohmann::ordered_json& json)
{
    if (!(std::ofstream(path) << json.dump(2) << '\n')) {
        Core::Log::error("Failed to write {}", path.string());
    }
}

/** Where a snapshot goes, and whether it was written. */
struct Snapshot
{
    std::string path;
    bool done = false;
    bool written = false;
};

/**
 * Writes `pixels` to the snapshot's file. Doesn't check a scene's known
 * pixels: tools/compare-renderings.mjs checks them on every platform.
 */
void writeSnapshot(Snapshot& snapshot, std::uint32_t widthPx, std::uint32_t heightPx,
                   const std::expected<std::vector<std::uint8_t>, std::string>& pixels)
{
    snapshot.done = true;
    if (!pixels) {
        Core::Log::error("{}", pixels.error());
        return;
    }
    if (auto written = Viewer::Png::write(snapshot.path, widthPx, heightPx, *pixels); !written) {
        Core::Log::error("{}", written.error());
        return;
    }
    snapshot.written = true;
    Core::Log::info("Wrote {}", snapshot.path);
}

} // namespace

int main(int argc, char** argv)
{
    // Exceptions are off, so running out of memory can only abort: say why
    // first, and how much was in use, without allocating.
    std::set_new_handler([] {
        char message[128];
        const auto process = Core::Memory::getProcessBytes();
        std::snprintf(message, sizeof(message), "ERROR  : Out of memory, with %.1f MiB in use\n",
                      process ? static_cast<double>(*process) / 1048576.0 : 0.0);
        std::fputs(message, stderr);
        std::abort();
    });

    // The image tests' scenes, one name per line, for tools/render-native.sh.
    if (getOption(argc, argv, "--list-scenes")) {
        for (const auto& known : Viewer::Scene::getScenes()) {
            std::fputs(std::format("{}\n", known.name).c_str(), stdout);
        }
        return 0;
    }

    // A scene for image tests instead of the trades, drawn once at its own size.
    const Viewer::Scene* scene = nullptr;
    if (const auto name = getOption(argc, argv, "--scene=")) {
        scene = Viewer::Scene::find(*name);
        if (!scene) {
            std::string names;
            for (const auto& known : Viewer::Scene::getScenes()) {
                names += std::format("{}{}", names.empty() ? "" : ", ", known.name);
            }
            Core::Log::error("No scene called \"{}\"; there are {}", *name, names);
            return 1;
        }
    }
    // Saves the first frame as a PNG, then exits: 1 if it couldn't.
    std::optional<Snapshot> snapshot;
    if (auto path = getOption(argc, argv, "--snapshot=")) {
        snapshot = Snapshot{ .path = std::move(*path) };
    }

    std::optional<Core::Data::Trades> loaded;
    if (const char* path = getTradesPath(argc, argv)) {
        auto trades = Core::Data::readTradeFile(path);
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
    if (scene) {
        // Exactly the scene's size in physical pixels, even on a scaled screen.
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    }
    GLFWwindow* window = glfwCreateWindow(scene ? static_cast<int>(scene->widthPx) : 1280, scene ? static_cast<int>(scene->heightPx) : 720,
                                          "Trade Viewer", nullptr, nullptr);
    if (!window) {
        Core::Log::error("Failed to create a window");
        glfwTerminate();
        return 1;
    }

    const auto target = getSurfaceTarget(window);
    if (!target) {
        Core::Log::error("Unsupported windowing platform");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Natively, creation finishes before create() returns.
    std::unique_ptr<Viewer::App> app;
    Viewer::App::create({ .target = *target, .powerPreference = parsePowerPreference(argc, argv) }, [&](Viewer::App::Result result) {
        if (!result) {
            Core::Log::error("{}", result.error());
            return;
        }
        app = std::move(*result);
        const auto& info = app->getInfo();
        Core::Log::info("Using {} · {} · {}", info.device, info.backend, info.adapter);
    });
    if (!app) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Wakes the loop when the GPU is lost and again once it's back.
    app->setWakeCallback([] { glfwPostEmptyEvent(); });

    WindowState state{ .app = app.get() };
    if (scene) {
        int widthPx = 0;
        int heightPx = 0;
        glfwGetFramebufferSize(window, &widthPx, &heightPx);
        if (std::cmp_not_equal(widthPx, scene->widthPx) || std::cmp_not_equal(heightPx, scene->heightPx)) {
            Core::Log::warn("The window is {} × {} pixels, not the scene's {} × {}", widthPx, heightPx, scene->widthPx, scene->heightPx);
        }
        // A still picture: no input, and the scene's own size and pixel ratio.
        const auto patches = Viewer::Scene::show(*scene, *app);
        Core::Log::info("Scene {}: {}", scene->name, scene->description);
        if (snapshot) {
            // Beside the PNG: out.png's known pixels go to out.json, and
            // what drew it to about.json, which every scene's run rewrites.
            const std::filesystem::path path = snapshot->path;
            writeJson(std::filesystem::path(path).replace_extension(".json"), getSceneJson(*scene, patches));
            writeJson(path.parent_path() / "about.json", getAboutJson(app->getInfo()));
        }
    } else {
        if (auto set = app->setTrades(loaded ? std::move(*loaded) : Viewer::generateSyntheticDay()); !set) {
            Core::Log::error("{}", set.error());
            app.reset();
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }
        const auto stats = Core::Data::getStats(app->getTrades());
        Core::Log::info("{} {} trades from {} to {} UTC, {:.6f} in total", stats.count, loaded ? "loaded" : "synthetic",
                        formatUtc(stats.firstUs), formatUtc(stats.lastUs), stats.totalAmount);
        logMemory(*app);

        glfwSetWindowUserPointer(window, &state);
        connectInput(window);
        float contentScale = 1.0f;
        glfwGetWindowContentScale(window, &contentScale, nullptr);
        app->setPixelRatio(contentScale);
        Core::Log::info("Scroll to zoom, drag to pan, point at a trade for its details (in the title); R: 12:00-13:00, D: whole day, M: marker size by area or log, S: summaries on or off");
        logLegend(*app, state.markerScale);
    }
    if (snapshot) {
        // The next frame: the first.
        app->readPixels([&snapshot, scene, window](const std::expected<std::vector<std::uint8_t>, std::string>& pixels) {
            int widthPx = 0;
            int heightPx = 0;
            glfwGetFramebufferSize(window, &widthPx, &heightPx);
            writeSnapshot(*snapshot, scene ? scene->widthPx : static_cast<std::uint32_t>(widthPx),
                          scene ? scene->heightPx : static_cast<std::uint32_t>(heightPx), pixels);
        });
    }

    while (!glfwWindowShouldClose(window)) {
        // Sleep until the next event when there's nothing to draw.
        // GPU results that arrive meanwhile wait for that event, except a
        // snapshot's, which is polled for.
        if (snapshot && !snapshot->done) {
            glfwWaitEventsTimeout(0.001);
        } else if (!app->needsRender() && !state.repaint) {
            glfwWaitEvents();
        } else {
            glfwPollEvents();
        }
        app->processEvents();
        if (snapshot && snapshot->done) {
            break;
        }

        if (!scene) {
            int widthPx = 0;
            int heightPx = 0;
            glfwGetFramebufferSize(window, &widthPx, &heightPx);
            app->resize(static_cast<std::uint32_t>(widthPx), static_cast<std::uint32_t>(heightPx));
        }
        app->update(glfwGetTime() * 1000.0);
        if (app->needsRender() || state.repaint) {
            app->render();
            state.repaint = false;
            glfwSetWindowTitle(window, getWindowTitle(*app).c_str());
        }
    }

    app.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return snapshot && !snapshot->written ? 1 : 0;
}
