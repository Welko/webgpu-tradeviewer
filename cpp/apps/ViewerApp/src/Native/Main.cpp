#include "Native/Options.hpp"
#include "Native/Report.hpp"
#include "Native/Snapshot.hpp"
#include "Native/Window.hpp"
#include "Viewer/App.hpp"
#include "Viewer/Scene.hpp"

#include "Core/Data/TradeFile.hpp"
#include "Core/Log.hpp"
#include "Core/Memory.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using Viewer::Native::Window;

// The window's size when it shows trades, in window coordinates.
constexpr Window::SizePx DefaultSize{ .width = 1280, .height = 720 };

/**
 * Exceptions are off, so running out of memory can only abort: says why
 * first, and how much was in use, without allocating.
 */
void reportOutOfMemory()
{
    std::array<char, 128> message{};
    const auto process = Core::Memory::getProcessBytes();
    std::snprintf(message.data(), message.size(), "ERROR  : Out of memory, with %.1f MiB in use\n",
                  process ? static_cast<double>(*process) / (1024.0 * 1024.0) : 0.0);
    std::fputs(message.data(), stderr);
    std::abort();
}

/** The scenes' names, one per line, for tools/render-native.sh. */
void printScenes()
{
    for (const auto& scene : Viewer::Scene::getScenes())
    {
        std::fputs(std::format("{}\n", scene.name).c_str(), stdout);
    }
}

/** The scene called `name`, or null, with the names there are logged. */
const Viewer::Scene* findScene(const std::string& name)
{
    if (const auto* scene = Viewer::Scene::find(name))
    {
        return scene;
    }
    std::string names;
    for (const auto& scene : Viewer::Scene::getScenes())
    {
        names += std::format("{}{}", names.empty() ? "" : ", ", scene.name);
    }
    Core::Log::error("No scene called \"{}\"; there are {}", name, names);
    return nullptr;
}

/** The trades in the file at `path`, with its warnings logged, or nothing, with why logged. */
std::optional<Core::Data::Trades> readTrades(const std::string& path)
{
    auto parsed = Core::Data::readTradeFile(path);
    if (!parsed)
    {
        Core::Log::error("{}", parsed.error());
        return std::nullopt;
    }
    for (const auto& warning : parsed->warnings)
    {
        Core::Log::warn("{}", warning);
    }
    return std::move(parsed->trades);
}

/** An app drawing to `window`, or null, with why logged. Natively, creation finishes before create() returns. */
std::unique_ptr<Viewer::App> createApp(const Window& window, Gpu::Type::PowerPreference powerPreference)
{
    const auto target = window.getSurfaceTarget();
    if (!target)
    {
        Core::Log::error("Unsupported windowing platform");
        return nullptr;
    }
    std::unique_ptr<Viewer::App> app;
    Viewer::App::create({ .target = *target, .powerPreference = powerPreference }, [&](Viewer::App::Result result) {
        if (!result)
        {
            Core::Log::error("{}", result.error());
            return;
        }
        app = std::move(*result);
        const auto& info = app->getInfo();
        Core::Log::info("Using {} · {} · {}", info.device, info.backend, info.adapter);
    });
    return app;
}

/** Shows `scene`: a still picture, with no input, at the scene's own size and pixel ratio. */
void showScene(const Viewer::Scene& scene, const Window& window, Viewer::App& app, const std::optional<Viewer::Native::Snapshot>& snapshot)
{
    const auto sizePx = window.getSizePx();
    if (sizePx.width != scene.widthPx || sizePx.height != scene.heightPx)
    {
        Core::Log::warn("The window is {} × {} pixels, not the scene's {} × {}", sizePx.width, sizePx.height, scene.widthPx,
                        scene.heightPx);
    }
    const auto patches = Viewer::Scene::show(scene, app);
    Core::Log::info("Scene {}: {}", scene.name, scene.description);
    if (snapshot)
    {
        snapshot->writeSceneFiles(scene, patches, app.getInfo());
    }
}

/** Shows `loaded` trades, or else the synthetic day, and lets the window's input explore them. False if they can't be shown. */
bool showTrades(std::optional<Core::Data::Trades> loaded, Window& window, Viewer::App& app)
{
    const bool fromFile = loaded.has_value();
    if (auto set = app.setTrades(fromFile ? std::move(*loaded) : Viewer::generateSyntheticDay()); !set)
    {
        Core::Log::error("{}", set.error());
        return false;
    }
    Viewer::Native::logTrades(app, fromFile);
    Viewer::Native::logMemory(app);
    window.connect(app);
    Viewer::Native::logControls();
    Viewer::Native::logLegend(app);
    return true;
}

/** Draws until the window closes, or until `snapshot` has its frame. A scene keeps its size. */
void run(Window& window, Viewer::App& app, const std::optional<Viewer::Native::Snapshot>& snapshot, bool fixedSize)
{
    while (!window.shouldClose())
    {
        // Sleep until the next event when there's nothing to draw. GPU
        // results that arrive meanwhile wait for that event, except a
        // snapshot's, which is polled for.
        if (snapshot && !snapshot->isDone())
        {
            Window::waitForEvents(0.001);
        }
        else if (!app.needsRender() && !window.hasRepaintRequest())
        {
            Window::waitForEvents();
        }
        else
        {
            Window::pollEvents();
        }
        app.processEvents();
        if (snapshot && snapshot->isDone())
        {
            return;
        }

        if (!fixedSize)
        {
            const auto sizePx = window.getSizePx();
            app.setSize(sizePx.width, sizePx.height);
        }
        app.update(Window::getTimeMs());
        if (const bool repaint = window.takeRepaintRequest(); repaint || app.needsRender())
        {
            app.render();
            window.setTitle(Viewer::Native::getWindowTitle(app));
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    std::set_new_handler(reportOutOfMemory);

    const auto options = Viewer::Native::parseOptions(std::span<char* const>(argv + 1, static_cast<std::size_t>(argc - 1)));
    if (options.listScenes)
    {
        printScenes();
        return 0;
    }
    const Viewer::Scene* scene = options.sceneName ? findScene(*options.sceneName) : nullptr;
    if (options.sceneName && !scene)
    {
        return 1;
    }
    std::optional<Core::Data::Trades> loaded;
    if (options.tradesPath)
    {
        loaded = readTrades(*options.tradesPath);
        if (!loaded)
        {
            return 1;
        }
    }
    // Saves the first frame as a PNG, then exits: 1 if it couldn't.
    std::optional<Viewer::Native::Snapshot> snapshot;
    if (options.snapshotPath)
    {
        snapshot.emplace(*options.snapshotPath);
    }

    auto window =
        Window::open(scene ? Window::SizePx{ .width = scene->widthPx, .height = scene->heightPx } : DefaultSize, scene != nullptr);
    if (!window)
    {
        Core::Log::error("{}", window.error());
        return 1;
    }
    // Declared after the window, so it's destroyed first.
    const auto app = createApp(*window, options.powerPreference);
    if (!app)
    {
        return 1;
    }
    // Wakes the loop when the GPU is lost and again once it's back.
    app->setWakeCallback(Window::wake);

    if (scene)
    {
        showScene(*scene, *window, *app, snapshot);
    }
    else if (!showTrades(std::move(loaded), *window, *app))
    {
        return 1;
    }
    if (snapshot)
    {
        // The next frame: the first, drawn at the scene's size or the window's.
        app->readPixels([&snapshot, &window, scene](const std::expected<std::vector<std::uint8_t>, std::string>& pixels) {
            const auto sizePx = scene ? Window::SizePx{ .width = scene->widthPx, .height = scene->heightPx } : window->getSizePx();
            snapshot->write(sizePx.width, sizePx.height, pixels);
        });
    }

    run(*window, *app, snapshot, scene != nullptr);
    return snapshot && !snapshot->isWritten() ? 1 : 0;
}
