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

/** `scene` drawn offscreen, or nothing, with a failed check, if the app can't start or the pixels don't arrive in 5 s. */
std::optional<Rendering> render(const Viewer::Scene& scene)
{
    std::unique_ptr<Viewer::App> app;
    Viewer::App::create(Gpu::Type::Target::Offscreen{ .width = scene.widthPx, .height = scene.heightPx },
        Gpu::Type::PowerPreference::Default, [&](Viewer::App::Result result) {
            CHECK_MESSAGE(result, (result ? "" : result.error()));
            if (result) {
                app = std::move(*result);
            }
        });
    if (!app) {
        return std::nullopt;
    }
    auto patches = Viewer::Scene::show(scene, *app);
    app->render();

    std::optional<Rendering> rendering;
    bool done = false;
    app->readPixels([&](std::expected<std::vector<std::uint8_t>, std::string> result) {
        done = true;
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result) {
            rendering = Rendering{ .pixels = std::move(*result), .patches = std::move(patches) };
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        app->processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(done, "The readback never completed");
    return rendering;
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
