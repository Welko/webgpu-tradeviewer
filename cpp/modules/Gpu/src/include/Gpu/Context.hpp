#pragma once

#include "Gpu/Handles.hpp"
#include "Gpu/Resource.hpp"
#include "Gpu/Type.hpp"

#include <webgpu/webgpu.h>

#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace Gpu {

/**
 * Internal to Gpu: the WebGPU instance, adapter, device and queue,
 * released on destruction. Other Gpu classes use the handles directly.
 * Where frames go is up to Surface, which is created from a Context.
 *
 * Everything that holds a tracked handle reporting to `resources`, and
 * every Surface, must be destroyed before the Context. The destructor
 * checks: if anything is left, it logs what and aborts, since those objects
 * would otherwise use the Context after it is gone.
 */
struct Context
{
    using Result = std::expected<std::unique_ptr<Context>, std::string>;
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
     * Requests an adapter and a device, then calls `onDone` exactly once.
     * Depending on the backend, `onDone` runs before this function returns
     * (wgpu-native) or later from the event loop (web).
     */
    static void create(Type::PowerPreference powerPreference, Callback onDone);

    // Declared first so it is destroyed last, after every handle below.
    Resource::Stats resources;

    // Members are destroyed in reverse order.
    Handle<WGPUInstance> instance;
    Handle<WGPUAdapter> adapter;
    Handle<WGPUDevice> device;
    Handle<WGPUQueue> queue;

    Features features;

    Type::Info info;

    Context() noexcept = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context();
};

} // namespace Gpu
