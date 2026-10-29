#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/Handle.hpp"
#include "Gpu/Resource.hpp"

#include <webgpu/webgpu.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace Gpu {

/**
 * A GPU buffer that data is appended to. When full, it moves to a bigger
 * buffer, copying on the GPU: slow, so reserve() room when the size is known.
 * The Context and Stats must outlive it.
 */
class GrowableBuffer
{
public:
    /** `usage` is what the data is for; copying in and out is added. Holds nothing until data arrives. */
    GrowableBuffer(const Context& context, Resource::Stats& stats, WGPUBufferUsage usage);

    /** Makes room for at least `bytes` in total, without growing again. */
    void reserve(std::uint64_t bytes);

    /** Adds `data` after what's there. Its size must be a multiple of 4. */
    void append(std::span<const std::byte> data);

    /** Forgets the data, keeping the room. */
    void clear() { mSize = 0; }

    /** Null until something is reserved or appended. */
    [[nodiscard]] WGPUBuffer get() const { return mBuffer.get(); }

    [[nodiscard]] std::uint64_t size() const { return mSize; }
    [[nodiscard]] std::uint64_t capacity() const { return mCapacity; }

private:
    /** Moves to a buffer of `capacity` bytes, keeping the data. */
    void grow(std::uint64_t capacity);

    const Context* mContext;
    Resource::Stats* mStats;
    WGPUBufferUsage mUsage;
    Handle<WGPUBuffer> mBuffer;
    std::uint64_t mCapacity = 0;
    std::uint64_t mSize = 0;
};

} // namespace Gpu
