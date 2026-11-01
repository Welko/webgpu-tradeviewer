#pragma once

#include "Gpu/Handle.hpp"
#include "Gpu/Type.hpp"

#include <webgpu/webgpu.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Submitting to a lost device crashes wgpu-native (a Rust panic), where
// browsers ignore it. So nothing submits but Context::submit(), which skips
// a lost device: with warnings as errors, other uses don't compile.
extern "C" {
[[deprecated("Submitting to a lost device crashes wgpu-native: use Gpu::Context::submit()")]]
void wgpuQueueSubmit(WGPUQueue queue, size_t commandCount, WGPUCommandBuffer const* commands);
}

namespace Gpu {

/** The device-wide WebGPU objects, and what we learned about the GPU. */
struct Context
{
    using Result = std::expected<Context, std::string>;
    using Callback = std::function<void(Result)>;

    /**
     * Requested only when the adapter offers them, so check before relying
     * on one. No defaults, so a new one can't be forgotten.
     */
    struct Features
    {
        bool timestampQuery;  // GPU timing
    };

    /**
     * What the device reported on its own, outside any call: errors nothing
     * caught, and its loss. Shared with the device's callbacks, which may
     * run after the context is gone.
     */
    struct Health
    {
        // The first errors, in order; more are only logged and counted.
        static constexpr std::size_t KeptErrors = 16;

        std::vector<std::string> errors;
        std::size_t errorCount = 0;
        std::optional<std::string> lost;  // why, once the device is lost
        // Set before the device is released on purpose, which loses it too.
        bool released = false;
        // Called once the device is lost, unless released on purpose.
        std::function<void()> onLost;
    };

    /**
     * Requests an adapter (able to draw to `compatible`, unless null) and a
     * device, then calls `onDone` exactly once: natively before returning,
     * on the web later.
     */
    static void create(Handle<WGPUInstance> instance, WGPUSurface compatible, Type::PowerPreference powerPreference,
                       Callback onDone);

    /** Submits `commands` to the queue, unless the device is lost: then they're dropped. */
    void submit(WGPUCommandBuffer commands) const;

    // Destroyed in reverse order.
    Handle<WGPUInstance> instance;
    Handle<WGPUAdapter> adapter;
    Handle<WGPUDevice> device;
    Handle<WGPUQueue> queue;

    Features features{};

    /**
     * Nanoseconds per GPU timestamp tick. wgpu-native reports raw ticks (83.3
     * ns each on Intel's Skylake GPUs, for example); browsers report
     * nanoseconds, so it stays 1 on the web.
     */
    double nanosecondsPerTick = 1.0;

    // The largest buffer the device takes: the adapter's most, often far
    // above WebGPU's default of 256 MiB.
    std::uint64_t maxBufferBytes = 256ull << 20;

    Type::Info info;

    std::shared_ptr<Health> health;
};

} // namespace Gpu
