#include "Gpu/Readback.hpp"

#include "Gpu/Convert.hpp"
#include "Gpu/Handles.hpp"

#include <cstddef>
#include <cstring>
#include <format>
#include <memory>
#include <utility>

namespace Gpu::Readback {

namespace {

constexpr std::uint32_t BytesPerPixel = 4;

// Texture-to-buffer copies need each row padded to 256 bytes.
constexpr std::uint32_t RowAlignment = 256;

/**
 * One texture() in flight. Owns itself while WebGPU holds it as userdata, so
 * it survives its caller being destroyed first.
 */
struct Pending
{
    Handle<WGPUBuffer> staging;
    std::uint32_t rowBytes;     // tight
    std::uint32_t bytesPerRow;  // padded
    std::uint32_t rows;
    Callback onDone;

    static void onMapped(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void*)
    {
        std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata1));
        if (status != WGPUMapAsyncStatus_Success) {
            pending->onDone(std::unexpected(
                std::format("Failed to read back from the GPU: {}", Convert::FromWgpu::stringView(message))));
            return;
        }

        const std::size_t size = std::size_t{ pending->bytesPerRow } * pending->rows;
        const auto* mapped = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(pending->staging.get(), 0, size));
        Bytes bytes(std::size_t{ pending->rowBytes } * pending->rows);
        for (std::uint32_t row = 0; row < pending->rows; ++row) {
            std::memcpy(bytes.data() + std::size_t{ row } * pending->rowBytes,
                        mapped + std::size_t{ row } * pending->bytesPerRow, pending->rowBytes);
        }
        wgpuBufferUnmap(pending->staging.get());
        pending->onDone(std::move(bytes));
    }
};

} // namespace

void texture(const Context& context, WGPUTexture texture, std::uint32_t width, std::uint32_t height, Callback onDone)
{
    if (!texture || width == 0 || height == 0) {
        onDone(std::unexpected("There is no texture to read back"));
        return;
    }

    const std::uint32_t rowBytes = width * BytesPerPixel;
    const std::uint32_t bytesPerRow = (rowBytes + RowAlignment - 1) / RowAlignment * RowAlignment;
    const std::uint64_t size = std::uint64_t{ bytesPerRow } * height;

    WGPUBufferDescriptor stagingDescriptor{};
    stagingDescriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    stagingDescriptor.size = size;
    auto pending = std::make_unique<Pending>(Handle(wgpuDeviceCreateBuffer(context.device.get(), &stagingDescriptor)),
                                             rowBytes, bytesPerRow, height, std::move(onDone));

    const Handle encoder(wgpuDeviceCreateCommandEncoder(context.device.get(), nullptr));
    WGPUTexelCopyTextureInfo source{};
    source.texture = texture;
    WGPUTexelCopyBufferInfo destination{};
    destination.buffer = pending->staging.get();
    destination.layout.bytesPerRow = bytesPerRow;
    destination.layout.rowsPerImage = height;
    const WGPUExtent3D extent{ width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder.get(), &source, &destination, &extent);
    const Handle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
    const WGPUCommandBuffer submitted = commands.get();
    wgpuQueueSubmit(context.queue.get(), 1, &submitted);

    // Read before the call: argument order is unspecified, and one argument
    // releases `pending`.
    const WGPUBuffer staging = pending->staging.get();
    wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, size, WGPUBufferMapCallbackInfo {
        .nextInChain = nullptr,
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = Pending::onMapped,
        .userdata1 = pending.release(),
        .userdata2 = nullptr,
    });
}

} // namespace Gpu::Readback
