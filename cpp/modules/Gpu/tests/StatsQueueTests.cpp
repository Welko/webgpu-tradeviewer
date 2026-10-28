#include "Gpu/Profiler.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace {

using StatsQueue = Gpu::Profiler::StatsQueue;
using Gpu::Type::FrameStats;
using UntimedReason = FrameStats::UntimedReason;

/** Frame `frame`, drawn and waiting for its GPU times. */
FrameStats drawn(std::uint64_t frame)
{
    return {
        .frame = frame,
        .cpu = std::nullopt,
        .gpu = std::unexpected(UntimedReason::Undefined),
        .buffers = 0,
        .textures = 0,
        .gpuBytes = 0,
    };
}

Gpu::Profiler::Times timed(double totalMs)
{
    return FrameStats::Gpu{ .totalMs = totalMs, .passes = {} };
}

/** The frame numbers in `reported`. */
std::vector<std::uint64_t> numbers(const std::vector<FrameStats>& reported)
{
    std::vector<std::uint64_t> frames;
    for (const auto& stats : reported) {
        frames.push_back(stats.frame);
    }
    return frames;
}

} // namespace

TEST_CASE("A frame is reported as soon as its times are in")
{
    std::vector<FrameStats> reported;
    StatsQueue queue(4);
    queue.setCallback([&reported](const FrameStats& stats) { reported.push_back(stats); });

    queue.push(drawn(1));
    CHECK(reported.empty());
    queue.setTimes(1, timed(2.0));
    CHECK(numbers(reported) == std::vector<std::uint64_t>{ 1 });
    if (reported.size() == 1 && reported[0].gpu) {
        CHECK(reported[0].gpu->totalMs == 2.0);
    }
}

TEST_CASE("Frames are reported in the order they were drawn")
{
    std::vector<FrameStats> reported;
    StatsQueue queue(4);
    queue.setCallback([&reported](const FrameStats& stats) { reported.push_back(stats); });

    queue.push(drawn(1));
    queue.push(drawn(2));
    queue.push(drawn(3));
    queue.setTimes(3, std::unexpected(UntimedReason::ProfilingOff));
    queue.setTimes(2, timed(1.0));
    CHECK(reported.empty());  // both wait for frame 1

    queue.setTimes(1, timed(1.0));
    CHECK(numbers(reported) == std::vector<std::uint64_t>{ 1, 2, 3 });
    if (reported.size() == 3) {
        CHECK(reported[2].gpu.error_or({}) == UntimedReason::ProfilingOff);
    }
}

TEST_CASE("When full, the oldest frame is reported without GPU times")
{
    std::vector<FrameStats> reported;
    StatsQueue queue(3);
    queue.setCallback([&reported](const FrameStats& stats) { reported.push_back(stats); });

    queue.push(drawn(1));
    queue.push(drawn(2));
    queue.push(drawn(3));
    queue.setTimes(2, timed(1.0));
    queue.setTimes(3, timed(1.0));
    CHECK(reported.empty());

    // Frame 1 makes room, and frames 2 and 3 no longer wait for it.
    queue.push(drawn(4));
    CHECK(numbers(reported) == std::vector<std::uint64_t>{ 1, 2, 3 });
    if (reported.size() == 3) {
        CHECK(reported[0].gpu.error_or({}) == UntimedReason::Overflow);
        CHECK(reported[1].gpu);
    }

    // Frame 1's times, arriving late, are ignored, even with the queue full.
    queue.push(drawn(5));
    queue.push(drawn(6));
    queue.setTimes(1, timed(1.0));
    CHECK(reported.size() == 3);
    queue.setTimes(4, timed(1.0));
    CHECK(numbers(reported) == std::vector<std::uint64_t>{ 1, 2, 3, 4 });
}

TEST_CASE("Frames completed before the callback is set aren't reported")
{
    std::vector<FrameStats> reported;
    StatsQueue queue(4);

    queue.push(drawn(1));
    queue.setTimes(1, timed(1.0));
    queue.push(drawn(2));
    queue.setCallback([&reported](const FrameStats& stats) { reported.push_back(stats); });
    queue.setTimes(2, timed(1.0));
    CHECK(numbers(reported) == std::vector<std::uint64_t>{ 2 });
}
