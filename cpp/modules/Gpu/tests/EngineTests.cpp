#include "Gpu/Market/Engine.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>

namespace {

using Gpu::Market::Engine;
using Pixels = Engine::Pixels;

// Tests are built without exceptions, so failures are reported with CHECK and
// the test returns early, instead of REQUIRE (which throws).

/** Null, with a failed check, if no engine could be created. */
std::unique_ptr<Engine> createOffscreen(std::uint32_t width, std::uint32_t height)
{
    std::unique_ptr<Engine> engine;
    bool done = false;
    Engine::create(Gpu::Type::Target::Offscreen{ width, height }, Gpu::Type::PowerPreference::Default,
        [&](Engine::Result result) {
            done = true;
            CHECK_MESSAGE(result, (result ? "" : result.error()));
            if (result) {
                engine = std::move(*result);
            }
        });
    // Natively, creation always finishes before create() returns.
    CHECK_MESSAGE(done, "Engine creation didn't finish before create() returned");
    return engine;
}

/**
 * Reads the offscreen pixels back, pumping events until they arrive or 5 s
 * pass. Nothing, with a failed check, if that fails.
 */
std::optional<Pixels> readPixels(Engine& engine)
{
    std::optional<Pixels> pixels;
    bool done = false;
    engine.readPixels([&](std::expected<Pixels, std::string> result) {
        done = true;
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result) {
            pixels = std::move(*result);
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        engine.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(done, "The readback never completed");
    return pixels;
}

/** The engine's background, (0.07, 0.08, 0.10) in 8-bit RGBA. */
void checkBackground(const Pixels& pixels, std::size_t pixel)
{
    CHECK((pixel + 1) * 4 <= pixels.size());
    if ((pixel + 1) * 4 > pixels.size()) {
        return;
    }
    const auto* rgba = pixels.data() + pixel * 4;
    CHECK(std::abs(rgba[0] - 18) <= 1);
    CHECK(std::abs(rgba[1] - 20) <= 1);
    CHECK(std::abs(rgba[2] - 26) <= 1);
    CHECK(rgba[3] == 255);
}

} // namespace

TEST_CASE("An offscreen engine draws its background into every pixel")
{
    const auto engine = createOffscreen(64, 16);
    if (!engine) {
        return;
    }
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    CHECK(pixels->size() == 64 * 16 * 4);
    checkBackground(*pixels, 0);
    checkBackground(*pixels, 64 * 16 - 1);

    const auto& stats = engine->stats();
    CHECK(stats.frame == 1);
    CHECK(stats.textures == 1);
    CHECK(stats.gpuBytes == 64 * 16 * 4);
}

TEST_CASE("Resizing an offscreen engine replaces its texture")
{
    const auto engine = createOffscreen(64, 16);
    if (!engine) {
        return;
    }
    engine->resize(32, 8);
    CHECK(engine->needsRender());
    engine->render();
    CHECK_FALSE(engine->needsRender());

    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    CHECK(pixels->size() == 32 * 8 * 4);
    checkBackground(*pixels, 32 * 8 - 1);
    CHECK(engine->stats().textures == 1);
    CHECK(engine->stats().gpuBytes == 32 * 8 * 4);
}

TEST_CASE("An offscreen target needs a size")
{
    std::optional<Engine::Result> result;
    Engine::create(Gpu::Type::Target::Offscreen{ 0, 16 }, Gpu::Type::PowerPreference::Default,
        [&](Engine::Result created) { result = std::move(created); });
    CHECK(result);
    if (result) {
        CHECK_FALSE(*result);
    }
}
