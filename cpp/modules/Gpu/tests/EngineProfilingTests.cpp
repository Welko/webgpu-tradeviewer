#include "EngineTesting.hpp"

#include "Core/Profiling.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

using namespace EngineTesting;

using UntimedReason = FrameStats::UntimedReason;

TEST_CASE("Profiling reports CPU times and the GPU time of each pass")
{
    if constexpr (!Core::Profiling::Compiled)
    {
        return;
    }
    const auto engine = createOffscreen(64, 16);
    if (!engine)
    {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    engine->setProfiling(Engine::ProfilingOn{});
    engine->render();
    if (!waitForStats(*engine, reported, 1))
    {
        return;
    }

    const auto& stats = reported[0];
    CHECK(stats.cpu);
    // CI relies on this: it fails, rather than skips, on a device without
    // timestamp-query.
    CHECK_MESSAGE(stats.gpu, "No GPU timings arrived; the device needs timestamp-query");
    if (stats.gpu)
    {
        CHECK(stats.gpu->passes.size() == 2);
        if (stats.gpu->passes.size() == 2)
        {
            CHECK(stats.gpu->passes[0].name == "grid");
            CHECK(stats.gpu->passes[1].name == "trades");
        }
    }
}

TEST_CASE("Each frame is reported once, in order, with times only if it was profiled")
{
    const auto engine = createOffscreen(16, 16);
    if (!engine)
    {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);

    engine->render();
    engine->setProfiling(Engine::ProfilingOn{});
    engine->render();
    engine->setProfiling(Engine::ProfilingOff{});
    // Untimed, but still reported after frame 2, whose GPU times are on their way.
    engine->render();
    if (!waitForStats(*engine, reported, 3))
    {
        return;
    }
    for (std::size_t i = 0; i < reported.size(); ++i)
    {
        CHECK(reported[i].frame == i + 1);
    }
    CHECK_FALSE(reported[0].cpu);
    CHECK(reported[0].gpu.error_or({}) == UntimedReason::ProfilingOff);
    CHECK(reported[1].cpu.has_value() == Core::Profiling::Compiled);
    CHECK(reported[1].gpu.has_value() == Core::Profiling::Compiled);
    CHECK_FALSE(reported[2].cpu);
    CHECK(reported[2].gpu.error_or({}) == UntimedReason::ProfilingOff);
}

TEST_CASE("Profiling a share of frames on the GPU times every frame on the CPU, and says which it left out")
{
    if (!Core::Profiling::Compiled)
    {
        return;
    }
    const auto engine = createOffscreen(16, 16);
    if (!engine)
    {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);

    // None, then all of them.
    engine->setProfiling(Engine::ProfilingOn{ .gpuTimingProbability = 0.0 });
    engine->render();
    engine->render();
    engine->setProfiling(Engine::ProfilingOn{ .gpuTimingProbability = 1.0 });
    engine->render();
    if (!waitForStats(*engine, reported, 3))
    {
        return;
    }
    for (const auto& stats : reported)
    {
        CHECK(stats.cpu);
    }
    CHECK(reported[0].gpu.error_or({}) == UntimedReason::NotSampled);
    CHECK(reported[1].gpu.error_or({}) == UntimedReason::NotSampled);
    CHECK(reported[2].gpu.has_value());
}
