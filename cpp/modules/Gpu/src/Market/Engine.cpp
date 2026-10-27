#include "Gpu/Market/Engine.hpp"

#include "Gpu/Context.hpp"
#include "Gpu/Surface.hpp"

#include <webgpu/webgpu.h>

#include <utility>

namespace Gpu::Market {

namespace {

constexpr WGPUColor ClearColor{ 0.07, 0.08, 0.10, 1.0 };

} // namespace

struct Engine::Impl
{
    std::unique_ptr<Context> context;
    
    // Declared after the context, so it is destroyed first.
    std::unique_ptr<Surface> surface;
};

void Engine::create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone)
{
    Context::create(powerPreference, [target, onDone = std::move(onDone)](Context::Result context) {
        if (!context) {
            onDone(std::unexpected(std::move(context.error())));
            return;
        }
        auto surface = Surface::create(**context, target);
        if (!surface) {
            onDone(std::unexpected(std::move(surface.error())));
            return;
        }
        auto impl = std::make_unique<Impl>(std::move(*context), std::move(*surface));
        onDone(std::unique_ptr<Engine>(new Engine(std::move(impl))));
    });
}

Engine::Engine(std::unique_ptr<Impl> impl) noexcept
    : mImpl(std::move(impl))
{}

Engine::~Engine() = default;

const Gpu::Type::Info& Engine::getInfo() const
{
    return mImpl->context->info;
}

void Engine::resize(std::uint32_t width, std::uint32_t height)
{
    mImpl->surface->resize(width, height);
}

void Engine::render()
{
    auto& ctx = *mImpl->context;
    auto& surface = *mImpl->surface;
    const auto image = surface.acquire();
    if (!image) {
        return;
    }

    const Handle<WGPUCommandEncoder> encoder(wgpuDeviceCreateCommandEncoder(ctx.device.get(), nullptr));

    WGPURenderPassColorAttachment color{};
    color.view = image->view.get();
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

    surface.present();
}

} // namespace Gpu::Market
