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

} // namespace

void map(WGPUBuffer buffer, std::uint64_t size, Callback onDone)
{
    struct Mapping
    {
        Handle<WGPUBuffer> buffer;
        std::size_t size;
        Callback onDone;

        static void onMapped(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void* userdata2)
        {
            std::ignore = userdata2;

            const std::unique_ptr<Mapping> mapping(static_cast<Mapping*>(userdata1));
            if (status != WGPUMapAsyncStatus_Success) {
                mapping->onDone(std::unexpected(
                    std::format("Failed to read back from the GPU: {}", Convert::FromWgpu::stringView(message))));
                return;
            }

            const auto* mapped = static_cast<const std::uint8_t*>(
                wgpuBufferGetConstMappedRange(mapping->buffer.get(), 0, mapping->size));

            Bytes bytes(mapped, mapped + mapping->size);

            wgpuBufferUnmap(mapping->buffer.get());
            
            mapping->onDone(std::move(bytes));
        }
    };

    wgpuBufferAddRef(buffer);  // our own reference, released with the mapping
    auto mapping = std::make_unique<Mapping>(Mapping{
        .buffer = Handle(buffer),
        .size = static_cast<std::size_t>(size),
        .onDone = std::move(onDone),
    });

    wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, static_cast<std::size_t>(size), WGPUBufferMapCallbackInfo {
        .nextInChain = nullptr,
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = Mapping::onMapped,
        .userdata1 = mapping.release(),
        .userdata2 = nullptr,
    });
}

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
    const Handle staging(wgpuDeviceCreateBuffer(context.device.get(), &stagingDescriptor));

    const Handle encoder(wgpuDeviceCreateCommandEncoder(context.device.get(), nullptr));
    WGPUTexelCopyTextureInfo source{};
    source.texture = texture;
    WGPUTexelCopyBufferInfo destination{};
    destination.buffer = staging.get();
    destination.layout.bytesPerRow = bytesPerRow;
    destination.layout.rowsPerImage = height;
    const WGPUExtent3D extent{ width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder.get(), &source, &destination, &extent);
    const Handle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
    const WGPUCommandBuffer submitted = commands.get();
    wgpuQueueSubmit(context.queue.get(), 1, &submitted);

    // The copy's rows are padded; strip the padding once the bytes arrive.
    map(staging.get(), size, [rowBytes, bytesPerRow, height, onDone = std::move(onDone)](std::expected<Bytes, std::string> padded) {
        if (!padded) {
            onDone(std::unexpected(std::move(padded.error())));
            return;
        }
        Bytes bytes(std::size_t{ rowBytes } * height);
        for (std::uint32_t row = 0; row < height; ++row) {
            std::memcpy(bytes.data() + std::size_t{ row } * rowBytes,
                        padded->data() + std::size_t{ row } * bytesPerRow, rowBytes);
        }
        onDone(std::move(bytes));
    });
}

} // namespace Gpu::Readback
