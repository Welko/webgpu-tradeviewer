#include "Gpu/Market/Engine.hpp"

#include "Core/Profiling.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {

using Gpu::Market::Engine;
using Pixels = Engine::Pixels;
using Gpu::Type::FrameStats;
using UntimedReason = FrameStats::UntimedReason;

// Built without exceptions: CHECK and return early, instead of REQUIRE.

/** Null, with a failed check, if no engine could be created. */
std::unique_ptr<Engine> createOffscreen(std::uint32_t width, std::uint32_t height)
{
    std::unique_ptr<Engine> engine;
    bool done = false;
    Engine::create(Gpu::Type::Target::Offscreen{ .width = width, .height = height }, Gpu::Type::PowerPreference::Default,
        [&](Engine::CreatedResult result) {
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

/** The offscreen pixels, or nothing, with a failed check, if they don't arrive in 5 s. */
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

/** Collects every frame's stats as the engine reports them. */
void recordStats(Engine& engine, std::vector<FrameStats>& reported)
{
    engine.setStatsCallback([&reported](const FrameStats& stats) { reported.push_back(stats); });
}

/** Pumps events until `count` frames have been reported, or 5 s pass. */
void waitForStats(Engine& engine, const std::vector<FrameStats>& reported, std::size_t count)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (reported.size() < count && std::chrono::steady_clock::now() < deadline) {
        engine.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(reported.size() == count, "Not every frame was reported");
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
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    CHECK(pixels->size() == 64 * 16 * 4);
    checkBackground(*pixels, 0);
    checkBackground(*pixels, 64 * 16 - 1);

    // Untimed, so reported right away.
    CHECK(reported.size() == 1);
    if (reported.size() == 1) {
        CHECK(reported[0].frame == 1);
        CHECK(reported[0].textures == 1);
        CHECK(reported[0].gpuBytes == 64 * 16 * 4);
    }
}

TEST_CASE("Resizing an offscreen engine replaces its texture")
{
    const auto engine = createOffscreen(64, 16);
    if (!engine) {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
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
    CHECK(reported.size() == 1);
    if (reported.size() == 1) {
        CHECK(reported[0].textures == 1);
        CHECK(reported[0].gpuBytes == 32 * 8 * 4);
    }
}

TEST_CASE("Profiling reports CPU times and the GPU time of each pass")
{
    const auto engine = createOffscreen(64, 16);
    if (!engine) {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    engine->setProfiling(true);
    engine->render();
    waitForStats(*engine, reported, 1);

    CHECK(reported.size() == 1);
    if (reported.size() != 1) {
        return;
    }
    const auto& stats = reported[0];
    CHECK(stats.frame == 1);
    if constexpr (!Core::Profiling::Compiled) {
        CHECK_FALSE(stats.cpu);
        CHECK(stats.gpu.error_or({}) == UntimedReason::ProfilingOff);
        return;
    }
    CHECK(stats.cpu);
    // CI relies on this: it fails, rather than skips, on a device without
    // timestamp-query.
    CHECK_MESSAGE(stats.gpu, "No GPU timings arrived; the device needs timestamp-query");
    if (!stats.gpu) {
        return;
    }
    CHECK(stats.gpu->passes.size() == 1);
    if (stats.gpu->passes.size() == 1) {
        CHECK(stats.gpu->passes[0].name == "clear");
        CHECK(stats.gpu->passes[0].gpuMs >= 0.0);
        CHECK(stats.gpu->passes[0].gpuMs < 1000.0);  // a clear can't take a second
        CHECK(stats.gpu->totalMs == stats.gpu->passes[0].gpuMs);
    }
}

TEST_CASE("Each frame is reported once, in order, with times only if it was profiled")
{
    const auto engine = createOffscreen(16, 16);
    if (!engine) {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);

    engine->render();
    engine->setProfiling(true);
    engine->render();
    engine->setProfiling(false);
    // Untimed, but still reported after frame 2, whose GPU times are on their way.
    engine->render();
    waitForStats(*engine, reported, 3);

    CHECK(reported.size() == 3);
    if (reported.size() != 3) {
        return;
    }
    for (std::size_t i = 0; i < reported.size(); ++i) {
        CHECK(reported[i].frame == i + 1);
    }
    CHECK_FALSE(reported[0].cpu);
    CHECK(reported[0].gpu.error_or({}) == UntimedReason::ProfilingOff);
    CHECK(reported[1].cpu.has_value() == Core::Profiling::Compiled);
    CHECK(reported[1].gpu.has_value() == Core::Profiling::Compiled);
    CHECK_FALSE(reported[2].cpu);
    CHECK(reported[2].gpu.error_or({}) == UntimedReason::ProfilingOff);
}

TEST_CASE("An offscreen target needs a size")
{
    std::optional<Engine::CreatedResult> result;
    Engine::create(Gpu::Type::Target::Offscreen{ .width = 0, .height = 16 }, Gpu::Type::PowerPreference::Default,
        [&](Engine::CreatedResult created) { result = std::move(created); });
    CHECK(result);
    if (result) {
        CHECK_FALSE(*result);
    }
}
