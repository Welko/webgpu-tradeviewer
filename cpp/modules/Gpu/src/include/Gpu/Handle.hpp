#pragma once

#include "Gpu/Resource.hpp"

#include <webgpu/webgpu.h>

#include <cstdint>
#include <utility>

namespace Gpu {

/**
 * Owns one WebGPU object and releases it when destroyed. Move-only. A
 * tracked handle reports its kind and size to a Resource::Stats, which must
 * outlive it.
 */
template <typename T>
class Handle
{
public:
    /**
     * Specialized per WebGPU handle type (see below):
     * - `static void release(T)` releases the object;
     * - `static constexpr Resource::Kind Kind`, only for types
     *   Resource::Stats tracks.
     */
    struct Traits;

private:
    /**
     * Whether T can be tracked. A function, so it's evaluated only once
     * Traits is defined; declared before the requires-clause that uses it.
     */
    static constexpr bool trackable() { return requires { Traits::Kind; }; }

public:
    Handle() = default;

    /** Takes ownership of `raw`, which may be null. Not tracked. */
    explicit Handle(T raw) noexcept
        : mRaw(raw)
    {}

    /** Takes ownership of `raw` and reports `bytes` to `stats` while alive. */
    Handle(T raw, Resource::Stats& stats, std::uint64_t bytes) noexcept
        requires(trackable())
        : mRaw(raw),
          mStats(raw ? &stats : nullptr),
          mBytes(raw ? bytes : 0)
    {
        if (mStats) {
            mStats->add(Traits::Kind, mBytes);
        }
    }

    ~Handle()
    {
        reset();
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept
        : mRaw(std::exchange(other.mRaw, nullptr)),
          mStats(std::exchange(other.mStats, nullptr)),
          mBytes(std::exchange(other.mBytes, 0))
    {}

    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other) {
            reset();
            mRaw = std::exchange(other.mRaw, nullptr);
            mStats = std::exchange(other.mStats, nullptr);
            mBytes = std::exchange(other.mBytes, 0);
        }
        return *this;
    }

    [[nodiscard]] T get() const { return mRaw; }

    explicit operator bool() const { return mRaw != nullptr; }

    /** Releases the object now, if there is one, and becomes empty. */
    void reset() noexcept
    {
        if (!mRaw) {
            return;
        }
        if constexpr (trackable()) {
            if (mStats) {
                mStats->remove(Traits::Kind, mBytes);
            }
        }
        Traits::release(mRaw);
        mRaw = nullptr;
        mStats = nullptr;
        mBytes = 0;
    }

private:
    T mRaw = nullptr;
    Resource::Stats* mStats = nullptr;
    std::uint64_t mBytes = 0;
};

// Objects that hold no significant GPU memory of their own: never tracked.
#define GPU_HANDLE_TRAITS(Type, Release)                          \
    template <>                                                   \
    struct Handle<Type>::Traits                                   \
    {                                                             \
        static void release(Type raw) noexcept { Release(raw); }  \
    };

// Objects whose memory Resource::Stats can track, per handle.
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
