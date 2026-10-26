#include "Gpu/Market/Engine.hpp"

#include "Gpu/Context.hpp"

#include "Core/Log.hpp"

#include <webgpu/webgpu.h>

#include <utility>

namespace Gpu::Market {

namespace {

constexpr WGPUColor ClearColor{ 0.07, 0.08, 0.10, 1.0 };

} // namespace

struct Engine::Impl
{
    std::unique_ptr<Context> context;
};

void Engine::create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone)
{
    Context::create(target, powerPreference, [onDone = std::move(onDone)](Context::Result result) {
        if (!result) {
            onDone(std::unexpected(std::move(result.error())));
            return;
        }
        onDone(std::unique_ptr<Engine>(new Engine(std::make_unique<Impl>(std::move(*result)))));
    });
}

Engine::Engine(std::unique_ptr<Impl> impl)
    : mImpl(std::move(impl))
{}

Engine::~Engine() = default;

const Gpu::Type::Info& Engine::getInfo() const
{
    return mImpl->context->info;
}

void Engine::resize(std::uint32_t width, std::uint32_t height)
{
    mImpl->context->resize(width, height);
}

void Engine::render()
{
    auto& ctx = *mImpl->context;
    if (ctx.width == 0 || ctx.height == 0) {
        return;
    }

    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(ctx.surface, &surfaceTexture);

    switch (surfaceTexture.status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        break;
    case WGPUSurfaceGetCurrentTextureStatus_Outdated:
    case WGPUSurfaceGetCurrentTextureStatus_Lost:
        if (surfaceTexture.texture) {
            wgpuTextureRelease(surfaceTexture.texture);
        }
        ctx.configure();
        return;
    default:
        Core::Log::error("Failed to acquire the surface texture (status {})", static_cast<int>(surfaceTexture.status));
        if (surfaceTexture.texture) {
            wgpuTextureRelease(surfaceTexture.texture);
        }
        return;
    }

    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device, nullptr);

    WGPURenderPassColorAttachment color{};
    color.view = view;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = ClearColor;

    WGPURenderPassDescriptor passDescriptor{};
    passDescriptor.colorAttachmentCount = 1;
    passDescriptor.colorAttachments = &color;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(ctx.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);

#ifndef __EMSCRIPTEN__
    // The browser presents the canvas on its own.
    wgpuSurfacePresent(ctx.surface);
#endif

    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);
}

} // namespace Gpu::Market
