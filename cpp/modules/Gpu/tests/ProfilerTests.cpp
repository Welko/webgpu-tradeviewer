#include "Gpu/Context.hpp"
#include "Gpu/Profiler.hpp"

#include "Core/Profile.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using Gpu::Profiler;
using Gpu::Type::FrameStats;
using UntimedReason = FrameStats::UntimedReason;

// The profiler's private limits.
constexpr std::size_t Slots = 3;
constexpr std::uint32_t MaxPasses = 8;
constexpr std::size_t MaxPendingFrames = 16;

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

/** Adds frame `frame` to `reported`, with its GPU times. */
Profiler::StatsCallback report(std::vector<FrameStats>& reported, std::uint64_t frame)
{
    return [&reported, frame](Profiler::Times times) {
        reported.push_back({
            .frame = frame,
            .cpu = std::nullopt,
            .gpu = std::move(times),
            .buffers = 0,
            .textures = 0,
            .gpuBytes = 0,
        });
    };
}

/**
 * Goes through frame `frame`, timed, with a pass if `pass`, reporting it to
 * `reported`; its commands are never submitted. Whether the pass was timed.
 */
bool draw(Profiler& profiler, const Gpu::Context& context, std::vector<FrameStats>& reported, std::uint64_t frame,
          bool pass)
{
    const Gpu::Handle encoder(wgpuDeviceCreateCommandEncoder(context.device.get(), nullptr));
    profiler.beginFrame(true);
    const bool timed = pass && profiler.measurePass("pass") != nullptr;
    profiler.endPasses(encoder.get());
    profiler.endFrame(report(reported, frame));
    return timed;
}

/** Processes WebGPU events until `reported` holds `count` frames, or 5 s pass. */
void waitFor(const Gpu::Context& context, const std::vector<FrameStats>& reported, std::size_t count)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (reported.size() < count && std::chrono::steady_clock::now() < deadline) {
        wgpuInstanceProcessEvents(context.instance.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
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

TEST_CASE("An unfinished frame is dropped, freeing its slot")
{
    if constexpr (!Core::Profile::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    const auto profiler = Profiler::create(*context);
    const Gpu::Handle encoder(wgpuDeviceCreateCommandEncoder(context->device.get(), nullptr));

    // Otherwise the slots would all stay busy, and the last frame go untimed.
    for (std::size_t frame = 0; frame < 2 * Slots; ++frame) {
        profiler->beginFrame(true);
        CHECK(profiler->measurePass("pass"));
        SUBCASE("Begun again") {}
        SUBCASE("Begun again after its passes")
        {
            profiler->endPasses(encoder.get());
        }
    }
    profiler->beginFrame(true);
    CHECK(profiler->measurePass("pass"));
}

TEST_CASE("Frames are reported in order, once their times are in")
{
    if constexpr (!Core::Profile::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    const auto profiler = Profiler::create(*context);
    std::vector<FrameStats> reported;

    // Readbacks only arrive in processEvents(), so the frames' slots stay busy.
    CHECK(draw(*profiler, *context, reported, 1, true));
    CHECK(draw(*profiler, *context, reported, 2, true));
    CHECK(draw(*profiler, *context, reported, 3, true));
    CHECK_FALSE(draw(*profiler, *context, reported, 4, true));  // every slot is busy
    CHECK(reported.empty());  // frame 4 waits for frames 1 to 3

    waitFor(*context, reported, 4);
    CHECK(numbers(reported) == std::vector<std::uint64_t>{ 1, 2, 3, 4 });
    if (reported.size() == 4) {
        CHECK(reported[0].gpu);
        CHECK(reported[2].gpu);
        CHECK(reported[3].gpu.error_or({}) == UntimedReason::NoFreeSlot);
    }

    // Their slots are free again, with none of their passes left over.
    CHECK(draw(*profiler, *context, reported, 5, true));
    waitFor(*context, reported, 5);
    CHECK(reported.size() == 5);
    if (reported.size() == 5) {
        CHECK(reported[4].gpu);
        if (reported[4].gpu) {
            CHECK(reported[4].gpu->passes.size() == 1);
        }
    }
}

TEST_CASE("Frames whose times take too long are reported without them")
{
    if constexpr (!Core::Profile::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    const auto profiler = Profiler::create(*context);
    std::vector<FrameStats> reported;

    // Frames 1 to 3 hold the slots; the rest are untimed and wait behind
    // them, until the ring is full and pushes frames 1 to 3 out.
    std::vector<std::uint64_t> frames;
    for (std::uint64_t frame = 1; frame <= MaxPendingFrames + Slots; ++frame) {
        draw(*profiler, *context, reported, frame, true);
        frames.push_back(frame);
    }
    CHECK(numbers(reported) == frames);
    if (reported.size() == frames.size()) {
        CHECK(reported[0].gpu.error_or({}) == UntimedReason::Overflow);
        CHECK(reported[2].gpu.error_or({}) == UntimedReason::Overflow);
        CHECK(reported[3].gpu.error_or({}) == UntimedReason::NoFreeSlot);
    }

    // Their times, arriving now, are ignored, and their slots freed.
    for (int attempt = 0; attempt < 100; ++attempt) {
        wgpuInstanceProcessEvents(context->instance.get());
    }
    CHECK(reported.size() == frames.size());
    CHECK(draw(*profiler, *context, reported, frames.size() + 1, true));
}

TEST_CASE("An untimed frame is reported right away, with why")
{
    const auto context = createContext();
    if (!context) {
        return;
    }
    const auto profiler = Profiler::create(*context);
    std::vector<FrameStats> reported;

    profiler->beginFrame(false);
    CHECK_FALSE(profiler->measurePass("pass"));
    profiler->endPasses(nullptr);  // records nothing
    profiler->endFrame(report(reported, 1));
    CHECK(reported.size() == 1);
    if (reported.size() == 1) {
        CHECK(reported[0].gpu.error_or({}) == UntimedReason::ProfilingOff);
    }
}

TEST_CASE("A timed frame without passes is reported right away, with zero time")
{
    if constexpr (!Core::Profile::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    const auto profiler = Profiler::create(*context);
    std::vector<FrameStats> reported;

    draw(*profiler, *context, reported, 1, false);
    CHECK(reported.size() == 1);
    if (reported.size() == 1) {
        CHECK(reported[0].gpu);
        if (reported[0].gpu) {
            CHECK(reported[0].gpu->totalMs == 0.0);
            CHECK(reported[0].gpu->passes.empty());
        }
    }

    // Its slot is free right away: otherwise the next frames would fill them all.
    for (std::uint64_t frame = 2; frame <= Slots; ++frame) {
        draw(*profiler, *context, reported, frame, false);
    }
    CHECK(draw(*profiler, *context, reported, Slots + 1, true));
}

TEST_CASE("Times that arrive after the profiler is gone are dropped")
{
    if constexpr (!Core::Profile::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    std::vector<FrameStats> reported;
    {
        const auto profiler = Profiler::create(*context);
        draw(*profiler, *context, reported, 1, true);
    }
    for (int attempt = 0; attempt < 100; ++attempt) {
        wgpuInstanceProcessEvents(context->instance.get());
    }
    CHECK(reported.empty());
}

TEST_CASE("Each pass is timed with timestamps of its own, up to a limit")
{
    if constexpr (!Core::Profile::Compiled) {
        return;
    }
    const auto context = createContext();
    if (!context) {
        return;
    }
    const auto profiler = Profiler::create(*context);
    std::vector<FrameStats> reported;

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
    profiler->endFrame(report(reported, 1));

    waitFor(*context, reported, 1);
    CHECK(reported.size() == 1);
    if (reported.size() != 1) {
        return;
    }
    const auto& gpu = reported[0].gpu;
    CHECK(gpu);
    if (!gpu) {
        return;
    }
    CHECK(gpu->passes.size() == MaxPasses);
    double totalMs = 0.0;
    for (std::size_t pass = 0; pass < gpu->passes.size(); ++pass) {
        CHECK(gpu->passes[pass].name == "pass " + std::to_string(pass));
        CHECK(gpu->passes[pass].gpuMs > 0.0);
        totalMs += gpu->passes[pass].gpuMs;
    }
    CHECK(gpu->totalMs == doctest::Approx(totalMs));
}

TEST_CASE("Frames on a device without timestamp queries are untimed")
{
    auto context = createContext();
    if (!context) {
        return;
    }
    context->features.timestampQuery = false;
    const auto profiler = Profiler::create(*context);
    std::vector<FrameStats> reported;

    CHECK_FALSE(draw(*profiler, *context, reported, 1, true));
    CHECK(reported.size() == 1);
    if (reported.size() == 1) {
        const auto reason = Core::Profile::Compiled ? UntimedReason::Unsupported : UntimedReason::ProfilingOff;
        CHECK(reported[0].gpu.error_or({}) == reason);
    }
}

TEST_CASE("A frame that was never begun is untimed")
{
    const auto context = createContext();
    if (!context) {
        return;
    }
    const auto profiler = Profiler::create(*context);
    std::vector<FrameStats> reported;

    CHECK_FALSE(profiler->measurePass("pass"));
    profiler->endFrame(report(reported, 1));

    // Nor is a frame after one that was ended.
    profiler->beginFrame(false);
    profiler->endFrame(report(reported, 2));
    profiler->endFrame(report(reported, 3));

    CHECK(reported.size() == 3);
    if (reported.size() == 3) {
        CHECK(reported[0].gpu.error_or({}) == UntimedReason::NotStarted);
        CHECK(reported[2].gpu.error_or({}) == UntimedReason::NotStarted);
    }
}
