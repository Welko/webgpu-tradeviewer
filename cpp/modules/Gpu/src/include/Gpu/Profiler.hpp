#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/Handles.hpp"
#include "Gpu/Readback.hpp"
#include "Gpu/Type.hpp"

#include "Core/RingQueue.hpp"

#include <webgpu/webgpu.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Gpu {

class Profiler : public std::enable_shared_from_this<Profiler>
{
public:
    // Newer webgpu.h (emdawnwebgpu) shares one struct between render and
    // compute passes; wgpu-native 24 still has the render-only one.
#ifdef __EMSCRIPTEN__
    using TimestampWrites = WGPUPassTimestampWrites;
#else
    using TimestampWrites = WGPURenderPassTimestampWrites;
#endif

    using Times = std::expected<Type::FrameStats::Gpu, Type::FrameStats::UntimedReason>;
    using StatsCallback = std::function<void(Times)>;

    static std::shared_ptr<Profiler> create(const Context& context);

    void beginFrame(bool timed);

    [[nodiscard]] const TimestampWrites* measurePass(std::string_view name);

    void endPasses(WGPUCommandEncoder encoder);

    void endFrame(StatsCallback onStats);

private:
    static constexpr std::size_t NumSlots = 3; /// how many frames can be timed at once

    /** Where one timed frame's timestamps are read back to the CPU. */
    struct Slot
    {
        Handle<WGPUBuffer> buffer;  // mappable copy of the frame's timestamps
        std::vector<std::string> passNames;
        bool busy = false;  // from beginFrame() until its timestamps are read back
    };

    struct Timestamps
    {
        std::array<Slot, NumSlots> slots;
        Handle<WGPUQuerySet> querySet;
        Handle<WGPUBuffer> resolveBuffer;
    };

    struct CurrentFrame
    {
        Slot* slot;
        TimestampWrites timestampWrites;
    };

    struct QueuedFrame
    {
        bool ready;
        Times times;
        StatsCallback onStats;
    };

    std::expected<Timestamps, Type::FrameStats::UntimedReason> mTimestamps;
    std::expected<CurrentFrame, Type::FrameStats::UntimedReason> mCurrentFrame;
    // REVIEW: one heap allocation per frame, so the queue is no longer "allocated once". A
    //   frame's push count, kept by the profiler, finds it by position instead
    //   (index - (pushed - size())), and tells an evicted frame apart the same way.
    Core::RingQueue<std::shared_ptr<QueuedFrame>> mReadQueue;

    struct Dropped
    {
        size_t framesCount{0}; // TODO: Report this
        std::vector<std::string> passes{};
    } mDropped;
        
    explicit Profiler(const Context& context);

    void processQueue();
    std::weak_ptr<QueuedFrame> queueTimes(QueuedFrame frame);
};

} // namespace Gpu
