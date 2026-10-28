#include "Gpu/Context.hpp"
#include "Gpu/Profiler.hpp"

#include "Core/Profiling.hpp"

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

using Gpu::Profiler;
using UntimedReason = Gpu::Type::FrameStats::UntimedReason;

// The profiler's private limits.
constexpr std::size_t Slots = 3;
constexpr std::uint32_t MaxPasses = 8;

/** A frame's GPU times, as the profiler reported them. */
struct Reported
{
    std::uint64_t frame;
    Profiler::Times times;
};

/** Nothing, with a failed check, if no device could be created. */
std::optional<Gpu::Context> createContext()
{
    std::optional<Gpu::Context> context;
    Gpu::Context::create(Gpu::Handle(wgpuCreateInstance(nullptr)), nullptr, Gpu::Type::PowerPreference::Default,
        [&](Gpu::Context::Result result) {
            CHECK_MESSAGE(result, (result ? "" : result.error()));
            if (result) {
                context = std::move(*result);
            }
        });
    return context;
}

/** A profiler that adds each frame it reports to `reported`. */
std::shared_ptr<Profiler> createProfiler(const Gpu::Context& context, std::vector<Reported>& reported)
{
    return Profiler::create(context, [&reported](std::uint64_t frame, Profiler::Times times) {
        reported.push_back({ .frame = frame, .times = std::move(times) });
    });
}

/**
 * Goes through frame `frame`, timed, with a pass if `pass`; its commands are
 * never submitted. Whether the pass was timed.
 */
bool draw(Profiler& profiler, const Gpu::Context& context, std::uint64_t frame, bool pass)
{
    const Gpu::Handle encoder(wgpuDeviceCreateCommandEncoder(context.device.get(), nullptr));
    profiler.beginFrame(true);
    const bool timed = pass && profiler.measurePass("pass") != nullptr;
    profiler.endPasses(encoder.get());
    profiler.endFrame(frame);
    return timed;
}

/** Processes events until `count` frames have been reported. False, with a failed check, if that takes 5 s. */
bool waitFor(const Gpu::Context& context, const std::vector<Reported>& reported, std::size_t count)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (reported.size() < count && std::chrono::steady_clock::now() < deadline) {
        wgpuInstanceProcessEvents(context.instance.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(reported.size() == count, "Not every frame was reported");
    return reported.size() == count;
}

/** The frame numbers in `reported`, sorted. */
std::vector<std::uint64_t> numbers(const std::vector<Reported>& reported)
{
    std::vector<std::uint64_t> frames;
    for (const auto& frame : reported) {
        frames.push_back(frame.frame);
    }
    std::ranges::sort(frames);
    return frames;
}

/** Clears `view` in a render pass of its own, timed with `timestampWrites` unless null. */
void encodeClear(WGPUCommandEncoder encoder, WGPUTextureView view, const Profiler::TimestampWrites* timestampWrites)
{
    WGPURenderPassColorAttachment color{};
    color.view = view;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor descriptor{};
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    descriptor.timestampWrites = timestampWrites;

    const Gpu::Handle pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));
    wgpuRenderPassEncoderEnd(pass.get());
}

} // namespace

TEST_CASE("An unfinished frame is dropped, freeing its slot")
{
    if constexpr (!Core::Profiling::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    std::vector<Reported> reported;
    const auto profiler = createProfiler(*context, reported);
    const Gpu::Handle encoder(wgpuDeviceCreateCommandEncoder(context->device.get(), nullptr));

    // Otherwise the slots would run out. Every other frame ends its passes first.
    for (std::size_t frame = 0; frame < 2 * Slots; ++frame) {
        profiler->beginFrame(true);
        CHECK(profiler->measurePass("pass"));
        if (frame % 2 == 1) {
            profiler->endPasses(encoder.get());
        }
    }
    profiler->beginFrame(true);
    CHECK(profiler->measurePass("pass"));
    CHECK(reported.empty());
}

TEST_CASE("A timed frame is reported once its times are read back, freeing its slot")
{
    if constexpr (!Core::Profiling::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    std::vector<Reported> reported;
    const auto profiler = createProfiler(*context, reported);

    // Readbacks only arrive while events are processed, so the slots stay busy.
    CHECK(draw(*profiler, *context, 1, true));
    CHECK(draw(*profiler, *context, 2, true));
    CHECK(draw(*profiler, *context, 3, true));
    CHECK_FALSE(draw(*profiler, *context, 4, true));
    CHECK(numbers(reported) == std::vector<std::uint64_t>{ 4 });
    if (reported.size() == 1) {
        CHECK(reported[0].times.error_or({}) == UntimedReason::NoFreeSlot);
    }

    if (!waitFor(*context, reported, 4)) {
        return;
    }
    CHECK(numbers(reported) == std::vector<std::uint64_t>{ 1, 2, 3, 4 });
    for (std::size_t i = 1; i < reported.size(); ++i) {
        CHECK(reported[i].times);
    }

    // The slots are free again, with none of their passes left over.
    CHECK(draw(*profiler, *context, 5, true));
    if (!waitFor(*context, reported, 5)) {
        return;
    }
    CHECK(reported[4].frame == 5);
    CHECK(reported[4].times);
    if (reported[4].times) {
        CHECK(reported[4].times->passes.size() == 1);
    }
}

TEST_CASE("An untimed frame is reported right away, with why")
{
    const auto context = createContext();
    if (!context) {
        return;
    }
    std::vector<Reported> reported;
    const auto profiler = createProfiler(*context, reported);

    // Never begun.
    CHECK_FALSE(profiler->measurePass("pass"));
    profiler->endFrame(1);

    profiler->beginFrame(false);
    CHECK_FALSE(profiler->measurePass("pass"));
    profiler->endPasses(nullptr);  // records nothing
    profiler->endFrame(2);

    // Ended already.
    profiler->endFrame(3);

    CHECK(reported.size() == 3);
    if (reported.size() == 3) {
        CHECK(reported[0].times.error_or({}) == UntimedReason::NotStarted);
        CHECK(reported[1].frame == 2);
        CHECK(reported[1].times.error_or({}) == UntimedReason::ProfilingOff);
        CHECK(reported[2].times.error_or({}) == UntimedReason::NotStarted);
    }
}

TEST_CASE("A timed frame without passes is reported right away, with zero time")
{
    if constexpr (!Core::Profiling::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    std::vector<Reported> reported;
    const auto profiler = createProfiler(*context, reported);

    draw(*profiler, *context, 1, false);
    CHECK(reported.size() == 1);
    if (reported.size() == 1) {
        CHECK(reported[0].times);
        if (reported[0].times) {
            CHECK(reported[0].times->totalMs == 0.0);
            CHECK(reported[0].times->passes.empty());
        }
    }

    // Its slot is free right away: otherwise the next frames would fill them all.
    for (std::uint64_t frame = 2; frame <= Slots; ++frame) {
        draw(*profiler, *context, frame, false);
    }
    CHECK(draw(*profiler, *context, Slots + 1, true));
}

TEST_CASE("Times that arrive after the profiler is gone are dropped")
{
    if constexpr (!Core::Profiling::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    std::vector<Reported> reported;
    {
        const auto profiler = createProfiler(*context, reported);
        draw(*profiler, *context, 1, true);
    }
    for (int attempt = 0; attempt < 100; ++attempt) {
        wgpuInstanceProcessEvents(context->instance.get());
    }
    CHECK(reported.empty());
}

TEST_CASE("Each pass is timed with timestamps of its own, up to a limit")
{
    if constexpr (!Core::Profiling::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    std::vector<Reported> reported;
    const auto profiler = createProfiler(*context, reported);

    // Big enough that clearing it takes a measurable time.
    WGPUTextureDescriptor descriptor{};
    descriptor.usage = WGPUTextureUsage_RenderAttachment;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = { 256, 256, 1 };
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;
    const Gpu::Handle texture(wgpuDeviceCreateTexture(context->device.get(), &descriptor));
    const Gpu::Handle view(wgpuTextureCreateView(texture.get(), nullptr));
    const Gpu::Handle encoder(wgpuDeviceCreateCommandEncoder(context->device.get(), nullptr));

    profiler->beginFrame(true);
    for (std::uint32_t pass = 0; pass < MaxPasses; ++pass) {
        const auto* timestampWrites = profiler->measurePass("pass " + std::to_string(pass));
        CHECK(timestampWrites);
        if (timestampWrites) {
            CHECK(timestampWrites->beginningOfPassWriteIndex == 2 * pass);
            CHECK(timestampWrites->endOfPassWriteIndex == 2 * pass + 1);
        }
        encodeClear(encoder.get(), view.get(), timestampWrites);
    }
    // One pass too many: drawn, but not timed.
    const auto* untimed = profiler->measurePass("untimed");
    CHECK_FALSE(untimed);
    encodeClear(encoder.get(), view.get(), untimed);
    profiler->endPasses(encoder.get());

    const Gpu::Handle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
    const WGPUCommandBuffer submitted = commands.get();
    wgpuQueueSubmit(context->queue.get(), 1, &submitted);
    profiler->endFrame(1);

    if (!waitFor(*context, reported, 1)) {
        return;
    }
    const auto& times = reported[0].times;
    CHECK(times);
    if (!times) {
        return;
    }
    CHECK(times->passes.size() == MaxPasses);
    double totalMs = 0.0;
    for (std::size_t pass = 0; pass < times->passes.size(); ++pass) {
        CHECK(times->passes[pass].name == "pass " + std::to_string(pass));
        CHECK(times->passes[pass].gpuMs > 0.0);
        totalMs += times->passes[pass].gpuMs;
    }
    CHECK(times->totalMs == doctest::Approx(totalMs));
}

TEST_CASE("Frames on a device without timestamp queries are untimed")
{
    auto context = createContext();
    if (!context) {
        return;
    }
    context->features.timestampQuery = false;
    std::vector<Reported> reported;
    const auto profiler = createProfiler(*context, reported);

    CHECK_FALSE(draw(*profiler, *context, 1, true));
    CHECK(reported.size() == 1);
    if (reported.size() == 1) {
        const auto reason = Core::Profiling::Compiled ? UntimedReason::Unsupported : UntimedReason::ProfilingOff;
        CHECK(reported[0].times.error_or({}) == reason);
    }
}
