#pragma once

#include "Gpu/Handles.hpp"
#include "Gpu/Type.hpp"

#include <webgpu/webgpu.h>

#include <expected>
#include <functional>
#include <string>

namespace Gpu {

/**
 * Internal to Gpu: the device-wide WebGPU objects and what we learned about
 * the GPU. Plain data with one creation function; it knows nothing about
 * where frames go.
 */
struct Context
{
    using Result = std::expected<Context, std::string>;
    using Callback = std::function<void(Result)>;

    /**
     * Optional device features. Each is requested only when the adapter
     * offers it, so code must check before relying on one.
     */
    struct Features
    {
        bool timestampQuery = false;  // GPU timing
    };

    /**
     * Requests an adapter on `instance`, able to draw to `compatible` if it
     * isn't null, then a device, and calls `onDone` exactly once. Depending
     * on the backend, `onDone` runs before this function returns
     * (wgpu-native) or later from the event loop (web).
     */
    static void create(Handle<WGPUInstance> instance, WGPUSurface compatible, Type::PowerPreference powerPreference,
                       Callback onDone);

    // Destroyed in reverse order.
    Handle<WGPUInstance> instance;
    Handle<WGPUAdapter> adapter;
    Handle<WGPUDevice> device;
    Handle<WGPUQueue> queue;

    Features features;

    Type::Info info;
};

} // namespace Gpu
