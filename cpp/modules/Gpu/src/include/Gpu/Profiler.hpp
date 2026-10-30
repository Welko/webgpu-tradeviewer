#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/Handle.hpp"
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Gpu {

/** Times each frame's passes on the GPU with timestamp queries. */
class Profiler : public std::enable_shared_from_this<Profiler>
{
public:
    // Shared by render and compute passes.
    using TimestampWrites = WGPUPassTimestampWrites;

    using Times = std::expected<Type::FrameStats::Gpu, Type::FrameStats::UntimedReason>;
    using TimesCallback = std::function<void(std::uint64_t frame, Times times)>;

    /**
     * Calls `onTimes` once for each ended frame: from endFrame() if it's
     * untimed, otherwise once its timestamps are read back.
     */
    static std::shared_ptr<Profiler> create(const Context& context, TimesCallback onTimes);

    /**
     * GPU times from two timestamps per pass, begin and end, counted in ticks
     * of `nanosecondsPerTick`. A pass that ends before it begins counts as 0.
     */
    static Type::FrameStats::Gpu toGpuTimes(const std::vector<std::string>& passNames,
                                            std::span<const std::uint64_t> timestamps, double nanosecondsPerTick);

    void beginFrame(bool timed);

    [[nodiscard]] const TimestampWrites* measurePass(std::string_view name);

    void endPasses(WGPUCommandEncoder encoder);

    void endFrame(std::uint64_t frame);

    /**
     * Drawn frames' stats, waiting for their GPU times.
     * Reports each frame once, in the order they were drawn.
     */
    class StatsQueue
    {
    public:
        using Callback = std::function<void(const Type::FrameStats&)>;

        explicit StatsQueue(std::size_t capacity);

        void setCallback(Callback onStats);

        /**
         * Adds a drawn frame; frame numbers must go up by one. When full, the
         * oldest frame is reported first, without GPU times (Overflow).
         */
        void push(Type::FrameStats stats);

        /** Sets frame `frame`'s GPU times. Ignored if it was already reported. */
        void setTimes(std::uint64_t frame, Times times);

    private:
        struct Frame
        {
            Type::FrameStats stats;
            bool ready = false;  // once its GPU times are in
        };

        void reportReady();

        Core::RingQueue<Frame> mFrames;
        Callback mOnStats;
    };

private:
    static constexpr std::size_t NumSlots = 3;  // frames timed at once

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

    std::expected<Timestamps, Type::FrameStats::UntimedReason> mTimestamps;
    std::expected<CurrentFrame, Type::FrameStats::UntimedReason> mCurrentFrame;
    TimesCallback mOnTimes;
    double mNanosecondsPerTick;

    struct Dropped
    {
        std::vector<std::string> passes{};  // TODO: Report this
    } mDropped;

    Profiler(const Context& context, TimesCallback onTimes);
};

} // namespace Gpu
