#include "Gpu/Profiler.hpp"

#include "Core/Log.hpp"
#include "Core/Profiling.hpp"

#include <array>
#include <cstring>
#include <utility>

namespace Gpu {

namespace {

constexpr std::uint32_t MaxNumPasses = 8;
constexpr std::uint32_t MaxNumTimestamps = 2 * MaxNumPasses;

using UntimedReason = Type::FrameStats::UntimedReason;

/** GPU times from the timestamps of passes named `passNames`, as read back. */
Type::FrameStats::Gpu gpuTimes(const std::vector<std::string>& passNames, const Readback::Bytes& bytes)
{
    std::array<std::uint64_t, MaxNumTimestamps> timestamps{};
    std::memcpy(timestamps.data(), bytes.data(), 2 * passNames.size() * sizeof(std::uint64_t));

    std::vector<Type::FrameStats::Gpu::PassTiming> passes;
    double totalMs = 0.0;
    for (std::size_t pass = 0; pass < passNames.size(); ++pass) {
        const std::uint64_t begin = timestamps[2 * pass];
        const std::uint64_t end = timestamps[2 * pass + 1];
        // Some drivers report end before begin for very short passes.
        const double ms = end > begin ? static_cast<double>(end - begin) / 1e6 : 0.0;
        passes.push_back({ .name = passNames[pass], .gpuMs = ms });
        totalMs += ms;
    }
    return { .totalMs = totalMs, .passes = std::move(passes) };
}

} // namespace

std::shared_ptr<Profiler> Profiler::create(const Context& context, TimesCallback onTimes)
{
    return std::shared_ptr<Profiler>(new Profiler(context, std::move(onTimes)));
}

Profiler::Profiler(const Context& context, TimesCallback onTimes)
    : mTimestamps(std::unexpected(UntimedReason::Undefined)),
      mCurrentFrame(std::unexpected(UntimedReason::NotStarted)),
      mOnTimes(std::move(onTimes))
{
    if (!Core::Profiling::Compiled)
    {
        mTimestamps = std::unexpected(UntimedReason::ProfilingOff);
        return;
    }

    if (!context.features.timestampQuery) {
        mTimestamps = std::unexpected(UntimedReason::Unsupported);
        return;
    }

    constexpr std::uint64_t TimestampBytes = MaxNumTimestamps * sizeof(std::uint64_t);

    WGPUQuerySetDescriptor querySetDescriptor{};
    querySetDescriptor.type = WGPUQueryType_Timestamp;
    querySetDescriptor.count = MaxNumTimestamps;
    auto querySet = Handle(wgpuDeviceCreateQuerySet(context.device.get(), &querySetDescriptor));

    WGPUBufferDescriptor resolveDescriptor{};
    resolveDescriptor.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
    resolveDescriptor.size = TimestampBytes;
    auto resolveBuffer = Handle(wgpuDeviceCreateBuffer(context.device.get(), &resolveDescriptor));

    mTimestamps = {
        .slots = {},
        .querySet = std::move(querySet),
        .resolveBuffer = std::move(resolveBuffer),
    };
    
    for (auto& slot : mTimestamps->slots) {
        WGPUBufferDescriptor slotDescriptor{};
        slotDescriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        slotDescriptor.size = TimestampBytes;
        slot.buffer = Handle(wgpuDeviceCreateBuffer(context.device.get(), &slotDescriptor));
    }
}

void Profiler::beginFrame(bool timed)
{
    if (mCurrentFrame) {
        Core::Log::warn("GPU profiling: beginFrame() called before endFrame(); dropping the unfinished frame");
        mCurrentFrame->slot->busy = false;
    }

    if (!mTimestamps)
    {
        mCurrentFrame = std::unexpected(mTimestamps.error());
        return;
    }

    if (!timed) {
        mCurrentFrame = std::unexpected(UntimedReason::ProfilingOff);
        return;
    }

    for (auto& slot : mTimestamps->slots) {
        if (!slot.busy) {
            slot.busy = true;
            slot.passNames.clear();
            mCurrentFrame = {
                .slot = &slot,
                .timestampWrites = {},
            };
            return;
        }
    }

    mCurrentFrame = std::unexpected(UntimedReason::NoFreeSlot);
}

const Profiler::TimestampWrites* Profiler::measurePass(std::string_view name)
{
    if (!mCurrentFrame)
    {
        return nullptr;
    }

    if (mCurrentFrame->slot->passNames.size() >= MaxNumPasses)
    {
        mDropped.passes.emplace_back(name);
        return nullptr;
    }

    auto& passNames = mCurrentFrame->slot->passNames;
    const auto pass = static_cast<std::uint32_t>(passNames.size());
    passNames.emplace_back(name);

    mCurrentFrame->timestampWrites = TimestampWrites{
#ifdef __EMSCRIPTEN__
        .nextInChain = nullptr,
#endif
        .querySet = mTimestamps->querySet.get(),
        .beginningOfPassWriteIndex = 2 * pass,
        .endOfPassWriteIndex = 2 * pass + 1,
    };

    return &mCurrentFrame->timestampWrites;
}

void Profiler::endPasses(WGPUCommandEncoder encoder)
{
    if (!mDropped.passes.empty()) {
        // TODO: Warn or something (throttle)
        mDropped.passes.clear();
    }

    if (!mCurrentFrame || mCurrentFrame->slot->passNames.empty()) {
        return;
    }

    Slot& slot = *mCurrentFrame->slot;
    const auto timestamps = static_cast<std::uint32_t>(2 * slot.passNames.size());

    wgpuCommandEncoderResolveQuerySet(encoder, mTimestamps->querySet.get(), 0, timestamps, mTimestamps->resolveBuffer.get(), 0);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, mTimestamps->resolveBuffer.get(), 0, slot.buffer.get(), 0,
                                         timestamps * sizeof(std::uint64_t));
}

void Profiler::endFrame(std::uint64_t frame)
{
    // Between frames, nothing is timed.
    const auto current = std::exchange(mCurrentFrame, std::unexpected(UntimedReason::NotStarted));
    if (!current)
    {
        mOnTimes(frame, std::unexpected(current.error()));
        return;
    }

    Slot& slot = *current->slot;
    if (slot.passNames.empty())
    {
        slot.busy = false;
        // Nothing was drawn, so nothing took any time.
        mOnTimes(frame, Type::FrameStats::Gpu{ .totalMs = 0.0, .passes = {} });
        return;
    }

    const std::uint64_t size = 2 * slot.passNames.size() * sizeof(std::uint64_t);
    Readback::map(slot.buffer.get(), size,
        [profilerWeak = weak_from_this(), slot = &slot, frame](std::expected<Readback::Bytes, std::string> bytes)
        {
            const auto profiler = profilerWeak.lock();
            if (!profiler)
            {
                return;
            }

            slot->busy = false;
            if (bytes)
            {
                profiler->mOnTimes(frame, gpuTimes(slot->passNames, *bytes));
            }
            else
            {
                profiler->mOnTimes(frame, std::unexpected(UntimedReason::ReadbackFailed));
            }
        });
}

Profiler::StatsQueue::StatsQueue(std::size_t capacity)
    : mFrames(capacity)
{}

void Profiler::StatsQueue::setCallback(Callback onStats)
{
    mOnStats = std::move(onStats);
}

void Profiler::StatsQueue::push(Type::FrameStats stats)
{
    auto evicted = mFrames.pushBack({ .stats = std::move(stats), .ready = false });
    if (!evicted) {
        return;
    }
    evicted->stats.gpu = std::unexpected(UntimedReason::Overflow);
    if (mOnStats) {
        mOnStats(evicted->stats);
    }
    // Frames that were waiting only for it.
    reportReady();
}

void Profiler::StatsQueue::setTimes(std::uint64_t frame, Times times)
{
    if (mFrames.empty() || frame < mFrames.front().stats.frame) {
        return;
    }
    auto& waiting = mFrames.at(frame - mFrames.front().stats.frame);
    waiting.stats.gpu = std::move(times);
    waiting.ready = true;
    reportReady();
}

void Profiler::StatsQueue::reportReady()
{
    while (!mFrames.empty() && mFrames.front().ready) {
        const auto frame = mFrames.popFront();
        if (mOnStats) {
            mOnStats(frame.stats);
        }
    }
}

} // namespace Gpu
