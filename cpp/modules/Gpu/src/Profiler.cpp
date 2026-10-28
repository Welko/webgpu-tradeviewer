#include "Gpu/Profiler.hpp"

#include "Core/Log.hpp"
#include "Core/Profile.hpp"

#include <cstring>
#include <utility>

namespace Gpu {

namespace {

constexpr std::uint32_t MaxNumPasses = 8;
constexpr std::uint32_t MaxNumTimestamps = 2 * MaxNumPasses;
constexpr std::size_t MaxReadQueueSize = 16;

using UntimedReason = Type::FrameStats::UntimedReason;

/** GPU times from the timestamps of passes named `passNames`, as read back. */
Type::FrameStats::Gpu gpuTimes(const std::vector<std::string>& passNames, const Readback::Bytes& bytes)
{
    std::vector<std::uint64_t> timestamps(2 * passNames.size());
    std::memcpy(timestamps.data(), bytes.data(), timestamps.size() * sizeof(std::uint64_t));

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

std::shared_ptr<Profiler> Profiler::create(const Context& context)
{
    return std::shared_ptr<Profiler>(new Profiler(context));
}

Profiler::Profiler(const Context& context)
    : mTimestamps(std::unexpected(UntimedReason::Undefined)),
      mCurrentFrame(std::unexpected(UntimedReason::NotStarted)),
      mReadQueue(MaxReadQueueSize)
{
    if (!Core::Profile::Compiled)
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

    // Select free slot
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
        // Return silently: profiling is disabled or not supported => no-op
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

void Profiler::endFrame(StatsCallback onStats)
{
    // Between frames, nothing is timed.
    const auto current = std::exchange(mCurrentFrame, std::unexpected(UntimedReason::NotStarted));
    if (!current)
    {
        queueTimes({
            .ready = true,
            .times = std::unexpected(current.error()),
            .onStats = std::move(onStats),
        });
        return;
    }

    Slot& slot = *current->slot;
    if (slot.passNames.empty())
    {
        slot.busy = false;

        // Nothing was drawn, so nothing took any time.
        queueTimes({
            .ready = true,
            .times = Type::FrameStats::Gpu{ .totalMs = 0.0, .passes = {} },
            .onStats = std::move(onStats),
        });
        return;
    }

    auto queuedWeak = queueTimes({
        .ready = false,
        .times = {},
        .onStats = std::move(onStats),
    });

    const std::uint64_t size = 2 * slot.passNames.size() * sizeof(std::uint64_t);
    Readback::map(slot.buffer.get(), size,
        [queuedWeak, profilerWeak = weak_from_this(), slot = &slot](
            std::expected<Readback::Bytes, std::string> bytes)
            {
                auto profiler = profilerWeak.lock();
                auto queued = queuedWeak.lock();

                if (!profiler)
                {
                    return;
                }

                slot->busy = false;

                if (!queued)
                {
                    return;
                }

                if (bytes) {
                    queued->times = gpuTimes(slot->passNames, *bytes);
                } else {
                    queued->times = std::unexpected(UntimedReason::ReadbackFailed);
                }
                queued->ready = true;

                profiler->processQueue();
            });
}

void Profiler::processQueue()
{
    while (!mReadQueue.empty() && mReadQueue.front()->ready)
    {
        const auto frame = mReadQueue.popFront();
        frame->onStats(std::move(frame->times));
    }
}

std::weak_ptr<Profiler::QueuedFrame> Profiler::queueTimes(QueuedFrame frame)
{
    if (mReadQueue.empty() && frame.ready)
    {
        frame.onStats(std::move(frame.times));
        return {};
    }

    auto queued = std::make_shared<QueuedFrame>(std::move(frame));
    auto evicted = mReadQueue.pushBack(queued);

    if (evicted.has_value())
    {
        (*evicted)->onStats(std::unexpected(UntimedReason::Overflow));
        ++mDropped.framesCount;
    }

    processQueue();

    return queued;
}

} // namespace Gpu
