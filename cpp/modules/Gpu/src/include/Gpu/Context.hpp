#pragma once

#include "Gpu/Handle.hpp"
#include "Gpu/Type.hpp"

#include <webgpu/webgpu.h>

#include <expected>
#include <functional>
#include <string>

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
     * Requests an adapter (able to draw to `compatible`, unless null) and a
     * device, then calls `onDone` exactly once: natively before returning,
     * on the web later.
     */
    static void create(Handle<WGPUInstance> instance, WGPUSurface compatible, Type::PowerPreference powerPreference,
                       Callback onDone);

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

    Type::Info info;
};

} // namespace Gpu
