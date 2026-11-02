#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/Handle.hpp"
#include "Gpu/Readback.hpp"
#include "Gpu/Resource.hpp"
#include "Gpu/Type.hpp"

#include <webgpu/webgpu.h>

#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace Gpu {

/**
 * Where frames go: a surface (window or canvas) or an offscreen texture;
 * draw() hides the difference. Made in two steps: prepare() before the
 * device exists, so the adapter can be chosen to draw to the surface, then
 * create() once it does.
 */
class RenderTarget
{
public:
    /** The first step: a window's or canvas's surface, or an offscreen size. */
    class Prepared
    {
    public:
        /** For choosing an adapter that can draw to it; null offscreen. */
        [[nodiscard]] WGPUSurface getSurface() const { return mSurface.get(); }

    private:
        friend class RenderTarget;

        Handle<WGPUSurface> mSurface;
        std::uint32_t mWidthPx = 0;  // offscreen only
        std::uint32_t mHeightPx = 0;
    };

    /** Creates the surface for a window or canvas, or checks an offscreen size. */
    [[nodiscard]] static std::expected<Prepared, std::string> prepare(WGPUInstance instance, const Type::Target::Any& target);

    /** Picks the format and makes the render target ready, once the device exists. `context` and `resources` must outlive it. */
    [[nodiscard]] static std::expected<RenderTarget, std::string> create(Prepared&& prepared, const Context& context,
                                                                         Resource::Stats& resources);

    ~RenderTarget();
    RenderTarget(RenderTarget&& other) noexcept = default;
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget& operator=(RenderTarget&&) = delete;

    [[nodiscard]] WGPUTextureFormat getFormat() const { return mFormat; }

    /** In physical pixels. A surface's is 0 × 0 until setSize(). */
    [[nodiscard]] std::uint32_t getWidthPx() const { return mWidthPx; }
    [[nodiscard]] std::uint32_t getHeightPx() const { return mHeightPx; }

    /** Zero sizes are ignored: offscreen, the target's own stays. */
    void setSize(std::uint32_t widthPx, std::uint32_t heightPx);

    /**
     * Draws a frame: gets the next image, calls `encode(encoder, view)` once
     * to record the frame's passes into `encoder`, drawing into `view`, then
     * submits, copies the frame back to each readPixels() waiting for it, and
     * shows it. Returns false without calling `encode` when there's no image
     * (zero size, or the surface was just reconfigured). If `timed`, writes
     * how long getting the image and submitting took to `times`. A template,
     * so `encode` is called directly, never copied or allocated.
     */
    template <std::invocable<WGPUCommandEncoder, WGPUTextureView> Encode>
    [[nodiscard]] bool draw(Encode&& encode, Type::FrameStats::Cpu& times, bool timed)
    {
        auto frame = beginFrame(times, timed);
        if (!frame) {
            return false;
        }
        encode(frame->encoder.get(), frame->view.get());
        endFrame(*frame, times, timed);
        return true;
    }

    /**
     * Copies a frame back as RGBA rows, top row first: offscreen, the last
     * one drawn; on a surface, the next one draw() draws. Returns whether
     * it waits for that frame.
     */
    [[nodiscard]] bool readPixels(Readback::Callback onDone);

private:
    /** One frame being drawn: the texture it draws into, and the encoder recording it. */
    struct Frame
    {
        Handle<WGPUTexture> texture;  // null offscreen, where the render target keeps its own
        Handle<WGPUTextureView> view;
        Handle<WGPUCommandEncoder> encoder;
    };

    RenderTarget(const Context& context, Resource::Stats& resources) noexcept;

    /** Configures the surface, or recreates the offscreen texture. */
    void configure();

    /** Gets this frame's image, timed as acquiring, and an encoder for it; or nothing to skip the frame. */
    [[nodiscard]] std::optional<Frame> beginFrame(Type::FrameStats::Cpu& times, bool timed);

    /** This frame's image, without an encoder yet: the offscreen texture, or the surface's next one. */
    [[nodiscard]] std::optional<Frame> acquire();

    /**
     * Submits `frame`'s commands, copies it back to each readPixels() waiting
     * for it, then shows it, all timed as submitting. Only native windows
     * need showing.
     */
    void endFrame(const Frame& frame, Type::FrameStats::Cpu& times, bool timed);

    const Context* mContext;
    Resource::Stats* mResources;
    Handle<WGPUSurface> mSurface;  // set for a window or canvas
    Handle<WGPUTexture> mOffscreen;  // set for an offscreen target
    WGPUTextureFormat mFormat = WGPUTextureFormat_Undefined;
    bool mReadable = false;  // whether the surface's textures can be copied back
    // Configured to be copied back, from the first readPixels() on: the extra
    // usage may cost drivers an optimization, so only tests pay it.
    bool mCopied = false;
    std::uint32_t mWidthPx = 0;
    std::uint32_t mHeightPx = 0;
    std::vector<Readback::Callback> mPendingReads;  // waiting for the next frame on a surface
};

} // namespace Gpu
