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
    wgpuSurfaceGetCurrentTexture(ctx.surface.get(), &surfaceTexture);
    // The surface owns the texture's memory, so it isn't tracked.
    const Handle<WGPUTexture> texture(surfaceTexture.texture);

    switch (surfaceTexture.status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        break;
    case WGPUSurfaceGetCurrentTextureStatus_Outdated:
    case WGPUSurfaceGetCurrentTextureStatus_Lost:
        ctx.configure();
        return;
    default:
        Core::Log::error("Failed to acquire the surface texture (status {})", static_cast<int>(surfaceTexture.status));
        return;
    }

    const Handle<WGPUTextureView> view(wgpuTextureCreateView(texture.get(), nullptr));
    const Handle<WGPUCommandEncoder> encoder(wgpuDeviceCreateCommandEncoder(ctx.device.get(), nullptr));

    WGPURenderPassColorAttachment color{};
    color.view = view.get();
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = ClearColor;

    WGPURenderPassDescriptor passDescriptor{};
    passDescriptor.colorAttachmentCount = 1;
    passDescriptor.colorAttachments = &color;

    {
        const Handle<WGPURenderPassEncoder> pass(wgpuCommandEncoderBeginRenderPass(encoder.get(), &passDescriptor));
        wgpuRenderPassEncoderEnd(pass.get());
    }

    const Handle<WGPUCommandBuffer> commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
    const WGPUCommandBuffer submitted = commands.get();
    wgpuQueueSubmit(ctx.queue.get(), 1, &submitted);

#ifndef __EMSCRIPTEN__
    // The browser presents the canvas on its own.
    wgpuSurfacePresent(ctx.surface.get());
#endif
}

} // namespace Gpu::Market
