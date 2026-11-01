#include "Gpu/GrowableBuffer.hpp"

#include <algorithm>
#include <cassert>
#include <format>

namespace Gpu {

GrowableBuffer::GrowableBuffer(const Context& context, Resource::Stats& stats, WGPUBufferUsage usage)
    : mContext(&context),
      mStats(&stats),
      mUsage(usage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc)
{}

namespace {

std::unexpected<std::string> tooLarge(std::uint64_t bytes, std::uint64_t maxBytes)
{
    return std::unexpected(std::format("{} bytes are more than the largest buffer this GPU takes, {} bytes", bytes, maxBytes));
}

} // namespace

std::expected<void, std::string> GrowableBuffer::reserve(std::uint64_t bytes)
{
    // Copies need sizes in multiples of 4.
    const std::uint64_t rounded = (bytes + 3) / 4 * 4;
    if (rounded > mContext->maxBufferBytes) {
        return tooLarge(rounded, mContext->maxBufferBytes);
    }
    if (rounded > mCapacity) {
        grow(rounded);
    }
    return {};
}

std::expected<void, std::string> GrowableBuffer::append(std::span<const std::byte> data)
{
    assert(data.size() % 4 == 0);
    if (data.empty()) {
        return {};
    }
    const std::uint64_t needed = mSize + data.size();
    if (needed > mContext->maxBufferBytes) {
        return tooLarge(needed, mContext->maxBufferBytes);
    }
    if (needed > mCapacity) {
        // Half again as much, so appending many times copies little overall,
        // short of the largest buffer the device takes.
        grow(std::max(needed, std::min(mCapacity + mCapacity / 2 / 4 * 4, mContext->maxBufferBytes / 4 * 4)));
    }
    wgpuQueueWriteBuffer(mContext->queue.get(), mBuffer.get(), mSize, data.data(), data.size());
    mSize = needed;
    return {};
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
        mContext->submit(commands.get());
    }
    // The GPU keeps the old buffer alive until the copy is done.
    mBuffer = std::move(buffer);
    mCapacity = capacity;
}

} // namespace Gpu
