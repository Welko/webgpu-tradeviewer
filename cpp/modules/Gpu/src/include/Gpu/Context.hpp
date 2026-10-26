#pragma once

#include "Gpu/Type.hpp"

#include <webgpu/webgpu.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace Gpu {

/**
 * Internal to Gpu: the WebGPU instance, surface, adapter, device and
 * queue, released on destruction. Other Gpu classes use the handles
 * directly.
 */
struct Context
{
    using Result = std::expected<std::unique_ptr<Context>, std::string>;
    using Callback = std::function<void(Result)>;

    /**
     * Requests an adapter and a device for `target`, then calls `onDone`
     * exactly once. Depending on the backend, `onDone` runs before this
     * function returns (wgpu-native) or later from the event loop (web).
     */
    static void create(const Type::Target::Any& target, Type::PowerPreference powerPreference, Callback onDone);

    WGPUInstance instance = nullptr;
    WGPUSurface surface = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    Type::Info info;

    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context();

    /** Reconfigures the surface if the size changed. Zero sizes are ignored. */
    void resize(std::uint32_t newWidth, std::uint32_t newHeight);

    /** Applies format, width and height to the surface. */
    void configure();
};

} // namespace Gpu
