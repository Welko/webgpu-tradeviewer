#include "Gpu/GrowableBuffer.hpp"

#include <algorithm>
#include <cassert>

namespace Gpu {

GrowableBuffer::GrowableBuffer(const Context& context, Resource::Stats& stats, WGPUBufferUsage usage)
    : mContext(&context),
      mStats(&stats),
      mUsage(usage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc)
{}

void GrowableBuffer::reserve(std::uint64_t bytes)
{
    if (bytes > mCapacity) {
        // Copies need sizes in multiples of 4.
        grow((bytes + 3) / 4 * 4);
    }
}

void GrowableBuffer::append(std::span<const std::byte> data)
{
    assert(data.size() % 4 == 0);
    if (data.empty()) {
        return;
    }
    const std::uint64_t needed = mSize + data.size();
    if (needed > mCapacity) {
        // Half again as much, so appending many times copies little overall.
        grow(std::max(needed, mCapacity + mCapacity / 2 / 4 * 4));
    }
    wgpuQueueWriteBuffer(mContext->queue.get(), mBuffer.get(), mSize, data.data(), data.size());
    mSize = needed;
}

void GrowableBuffer::grow(std::uint64_t capacity)
{
    WGPUBufferDescriptor descriptor{};
    descriptor.usage = mUsage;
    descriptor.size = capacity;
    Handle<WGPUBuffer> buffer(wgpuDeviceCreateBuffer(mContext->device.get(), &descriptor), *mStats, capacity);

    if (mSize > 0) {
        // Queued before any later write, so the data lands in order.
        const Handle encoder(wgpuDeviceCreateCommandEncoder(mContext->device.get(), nullptr));
        wgpuCommandEncoderCopyBufferToBuffer(encoder.get(), mBuffer.get(), 0, buffer.get(), 0, mSize);
        const Handle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
        const WGPUCommandBuffer submitted = commands.get();
        wgpuQueueSubmit(mContext->queue.get(), 1, &submitted);
    }
    // The GPU keeps the old buffer alive until the copy is done.
    mBuffer = std::move(buffer);
    mCapacity = capacity;
}

} // namespace Gpu
