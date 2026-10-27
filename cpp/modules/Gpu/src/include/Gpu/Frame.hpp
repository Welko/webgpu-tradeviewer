#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/Handles.hpp"
#include "Gpu/Surface.hpp"

#include <webgpu/webgpu.h>

#include <optional>

namespace Gpu {

/**
 * One frame being recorded: the surface image it draws into and the
 * command encoder the passes record into. begin() acquires the image,
 * submit() sends the commands and presents.
 */
class Frame
{
public:
    /**
     * Acquires `surface`'s next image and starts recording a frame drawn at
     * `timeMs`. Nothing if the surface skips this frame (see
     * Surface::acquire).
     */
    [[nodiscard]] static std::optional<Frame> begin(Context& context, Surface& surface, double timeMs);

    [[nodiscard]] WGPUCommandEncoder encoder() const { return mEncoder.get(); }

    /** The image to draw into, and its format. */
    [[nodiscard]] WGPUTextureView view() const { return mImage.view.get(); }
    [[nodiscard]] WGPUTextureFormat format() const { return mSurface.format(); }

    /** The time this frame shows, in milliseconds, for time-based effects. */
    [[nodiscard]] double timeMs() const { return mTimeMs; }

    /** Finishes recording, submits the commands and presents. Call once. */
    void submit();

private:
    Frame(Context& context, Surface& surface, Surface::Image image, Handle<WGPUCommandEncoder> encoder,
          double timeMs) noexcept;

    Context& mContext;
    Surface& mSurface;
    Surface::Image mImage;
    Handle<WGPUCommandEncoder> mEncoder;
    double mTimeMs;
};

} // namespace Gpu
