#include "Viewer/App.hpp"
#include "Viewer/Scene.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

/** `scene` drawn offscreen, or nothing, with a failed check, if the app can't start or the pixels don't arrive in 5 s. */
std::optional<std::vector<std::uint8_t>> render(const Viewer::Scene& scene)
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
    Viewer::Scene::show(scene, *app);
    app->render();

    std::optional<std::vector<std::uint8_t>> pixels;
    bool done = false;
    app->readPixels([&](std::expected<std::vector<std::uint8_t>, std::string> result) {
        done = true;
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result) {
            pixels = std::move(*result);
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        app->processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(done, "The readback never completed");
    return pixels;
}

} // namespace

TEST_CASE("Every scene's known pixels come out as expected, offscreen")
{
    for (const auto& scene : Viewer::Scene::getScenes()) {
        CAPTURE(scene.name);
        const auto pixels = render(scene);
        if (!pixels) {
            continue;
        }
        for (const auto& failure : Viewer::Scene::checkPatches(scene, *pixels)) {
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
