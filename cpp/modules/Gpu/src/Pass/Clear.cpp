#include "Gpu/Pass/Clear.hpp"

#include "Gpu/Frame.hpp"

namespace Gpu {

Pass::Clear::Clear(WGPUColor color) noexcept
    : mColor(color)
{}

void Pass::Clear::encode(Frame& frame)
{
    WGPURenderPassColorAttachment color{};
    color.view = frame.view();
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = mColor;

    WGPURenderPassDescriptor descriptor{};
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;

    // Clearing happens when the pass begins; there is nothing to draw.
    const Handle pass(wgpuCommandEncoderBeginRenderPass(frame.encoder(), &descriptor));
    wgpuRenderPassEncoderEnd(pass.get());
}

} // namespace Gpu
