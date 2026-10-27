#pragma once

#include "Gpu/Handle.hpp"

#include <webgpu/webgpu.h>

namespace Gpu {

// Objects that hold no significant GPU memory of their own: never tracked.
#define GPU_HANDLE_TRAITS(Type, Release)                          \
    template <>                                                   \
    struct Handle<Type>::Traits                                   \
    {                                                             \
        static void release(Type raw) noexcept { Release(raw); }  \
    };

// Objects whose memory Resource::Stats can track. Tracking is opt-in per
// handle.
#define GPU_TRACKED_HANDLE_TRAITS(Type, Release, KindValue)       \
    template <>                                                   \
    struct Handle<Type>::Traits                                   \
    {                                                             \
        static constexpr Resource::Kind Kind = KindValue;         \
        static void release(Type raw) noexcept { Release(raw); }  \
    };

GPU_HANDLE_TRAITS(WGPUInstance, wgpuInstanceRelease)
GPU_HANDLE_TRAITS(WGPUSurface, wgpuSurfaceRelease)
GPU_HANDLE_TRAITS(WGPUAdapter, wgpuAdapterRelease)
GPU_HANDLE_TRAITS(WGPUDevice, wgpuDeviceRelease)
GPU_HANDLE_TRAITS(WGPUQueue, wgpuQueueRelease)
GPU_HANDLE_TRAITS(WGPUTextureView, wgpuTextureViewRelease)
GPU_HANDLE_TRAITS(WGPUCommandEncoder, wgpuCommandEncoderRelease)
GPU_HANDLE_TRAITS(WGPUCommandBuffer, wgpuCommandBufferRelease)
GPU_HANDLE_TRAITS(WGPURenderPassEncoder, wgpuRenderPassEncoderRelease)

GPU_TRACKED_HANDLE_TRAITS(WGPUBuffer, wgpuBufferRelease, Resource::Kind::Buffer)
GPU_TRACKED_HANDLE_TRAITS(WGPUTexture, wgpuTextureRelease, Resource::Kind::Texture)
GPU_TRACKED_HANDLE_TRAITS(WGPUQuerySet, wgpuQuerySetRelease, Resource::Kind::QuerySet)

#undef GPU_HANDLE_TRAITS
#undef GPU_TRACKED_HANDLE_TRAITS

} // namespace Gpu
