#include "Viewer/App.hpp"
#include "Viewer/Scene.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

/** A scene drawn, and its known pixels. */
struct Rendering
{
    std::vector<std::uint8_t> pixels;
    std::vector<Viewer::Scene::Patch> patches;
};

/** An app drawing offscreen at `scene`'s size, or none, with a failed check. */
std::unique_ptr<Viewer::App> createApp(const Viewer::Scene& scene)
{
    const Gpu::Market::Engine::Params params{ .target = Gpu::Type::Target::Offscreen{ .width = scene.widthPx, .height = scene.heightPx } };
    std::unique_ptr<Viewer::App> created;
    Viewer::App::create(params, [&](Viewer::App::Result result) {
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result) {
            created = std::move(*result);
        }
    });
    return created;
}

/** Draws a frame and reads it back, or nothing, with a failed check, if the pixels don't arrive in 5 s. */
std::optional<std::vector<std::uint8_t>> drawAndRead(Viewer::App& app)
{
    app.render();
    std::optional<std::vector<std::uint8_t>> pixels;
    bool done = false;
    app.readPixels([&](std::expected<std::vector<std::uint8_t>, std::string> result) {
        done = true;
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result) {
            pixels = std::move(*result);
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(done, "The readback never completed");
    return pixels;
}

/** Fails a check for each WebGPU error nothing caught: a validation error means a bug. */
void checkNoGpuErrors(const Viewer::App& app)
{
    const auto health = app.getGpuHealth();
    for (const auto& error : health.errors) {
        FAIL_CHECK(error);
    }
    CHECK(health.errorCount == 0);
}

/** `scene` drawn offscreen, or nothing, with a failed check, if the app can't start or the pixels don't arrive in 5 s. */
std::optional<Rendering> render(const Viewer::Scene& scene)
{
    auto app = createApp(scene);
    if (!app) {
        return std::nullopt;
    }
    auto patches = Viewer::Scene::show(scene, *app);
    auto pixels = drawAndRead(*app);
    checkNoGpuErrors(*app);
    if (!pixels) {
        return std::nullopt;
    }
    return Rendering{ .pixels = std::move(*pixels), .patches = std::move(patches) };
}

} // namespace

TEST_CASE("Every scene's known pixels come out as expected, offscreen")
{
    for (const auto& scene : Viewer::Scene::getScenes()) {
        CAPTURE(scene.name);
        const auto rendering = render(scene);
        if (!rendering) {
            continue;
        }
        for (const auto& failure : Viewer::Scene::checkPatches(scene, rendering->patches, rendering->pixels)) {
            FAIL_CHECK(failure);
        }
    }
}

TEST_CASE("Scenes are found by name")
{
    CHECK(Viewer::Scene::find("opaque") != nullptr);
    CHECK(Viewer::Scene::find("nothing") == nullptr);
    for (const auto& scene : Viewer::Scene::getScenes()) {
        CHECK(!scene.patches.empty());
    }
}

TEST_CASE("The app's scenes know where their grid lines, ring and highlighted column fall")
{
    const auto whats = [](const Viewer::Scene& scene) {
        std::vector<std::string> found;
        const auto rendering = render(scene);
        for (const auto& patch : rendering ? rendering->patches : std::vector<Viewer::Scene::Patch>{}) {
            found.push_back(patch.what);
        }
        return found;
    };
    const auto count = [](const std::vector<std::string>& whats, std::string_view prefix) {
        return std::ranges::count_if(whats, [&](const std::string& what) { return what.starts_with(prefix); });
    };

    const auto hour = whats(*Viewer::Scene::find("hour"));
    CHECK(count(hour, "grid line at ") >= 3);
    CHECK(count(hour, "ring") == 1);
    CHECK(count(hour, "right margin") == 1);

    const auto day = whats(*Viewer::Scene::find("day"));
    CHECK(count(day, "grid line at ") >= 3);
    CHECK(count(day, "highlighted column") == 1);
}

TEST_CASE("After the GPU is lost, the app restarts it and draws the same picture again")
{
    const auto& scene = *Viewer::Scene::find("hour");
    auto app = createApp(scene);
    if (!app) {
        return;
    }
    Viewer::Scene::show(scene, *app);
    const auto before = drawAndRead(*app);

    bool woken = false;
    app->setWakeCallback([&] { woken = true; });
    CHECK(app->loseGpu());
    // wgpu-native notices on the next call to the device, browsers on their own.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!woken && std::chrono::steady_clock::now() < deadline) {
        app->render();
        app->processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(woken, "The app wasn't woken when the GPU was lost");
    app->update(0.0);  // restarts it

    const auto health = app->getGpuHealth();
    CHECK(health.restarts == 1);
    CHECK_FALSE(health.restarting);
    CHECK_MESSAGE(!health.failure, health.failure.value_or(""));
    CHECK(app->needsRender());
    const auto after = drawAndRead(*app);
    checkNoGpuErrors(*app);
    CHECK(before);
    CHECK(after);
    if (before && after) {
        CHECK(*before == *after);
    }
}
