#include "Gpu/Frame.hpp"

#include <utility>

namespace Gpu {

std::optional<Frame> Frame::begin(Context& context, Surface& surface, double timeMs)
{
    auto image = surface.acquire();
    if (!image) {
        return std::nullopt;
    }
    Handle encoder(wgpuDeviceCreateCommandEncoder(context.device.get(), nullptr));
    return Frame(context, surface, std::move(*image), std::move(encoder), timeMs);
}

Frame::Frame(Context& context, Surface& surface, Surface::Image image, Handle<WGPUCommandEncoder> encoder,
             double timeMs) noexcept
    : mContext(context),
      mSurface(surface),
      mImage(std::move(image)),
      mEncoder(std::move(encoder)),
      mTimeMs(timeMs)
{}

void Frame::submit()
{
    const Handle commands(wgpuCommandEncoderFinish(mEncoder.get(), nullptr));
    mEncoder.reset();
    const WGPUCommandBuffer submitted = commands.get();
    wgpuQueueSubmit(mContext.queue.get(), 1, &submitted);
    mSurface.present();
}

} // namespace Gpu
