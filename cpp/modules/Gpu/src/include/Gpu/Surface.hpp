#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/Handles.hpp"
#include "Gpu/Type.hpp"

#include <webgpu/webgpu.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace Gpu {

/**
 * Where frames go: a window or canvas (Onscreen), or natively a texture we
 * own (Offscreen). Created from a Context once it has a device; a Context can
 * have several. Each frame: acquire(), draw into the image, submit,
 * present().
 */
class Surface
{
public:
    /** The texture one frame draws into. */
    struct Image
    {
        // The surface's current texture; empty for Offscreen, which keeps
        // its texture.
        Handle<WGPUTexture> texture;
        Handle<WGPUTextureView> view;
    };

    class Onscreen;
#ifndef __EMSCRIPTEN__
    class Offscreen;
#endif

    /** Creates the kind of surface `target` asks for. */
    [[nodiscard]] static std::expected<std::unique_ptr<Surface>, std::string>
    create(Context& context, const Type::Target::Any& target);

    virtual ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    [[nodiscard]] virtual WGPUTextureFormat format() const = 0;

    /** Size in physical pixels. Zero sizes are ignored. */
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

    /**
     * The image to draw this frame into, or nothing if the frame should be
     * skipped. Call once per frame.
     */
    [[nodiscard]] virtual std::optional<Image> acquire() = 0;

    /** Shows the frame, after its commands are submitted. */
    virtual void present() = 0;

protected:
    /**
     * Counts this surface in `context.resources` until it is destroyed,
     * with 0 bytes: an Offscreen's texture is counted under textures, and an
     * Onscreen's swap chain belongs to the window system.
     */
    explicit Surface(Context& context) noexcept;

    Context& mContext;
};

/**
 * A native window or a web canvas, backed by a WebGPU surface. Acquiring
 * takes the next image of its swap chain; presenting gives it back.
 */
class Surface::Onscreen final : public Surface
{
public:
    /**
     * Creates the WebGPU surface and picks a format the adapter can draw
     * to it in. Fails if it can't. Unconfigured until the first resize().
     */
    [[nodiscard]] static std::expected<std::unique_ptr<Onscreen>, std::string>
    create(Context& context, const Type::Target::Any& target);

    ~Onscreen() override;

    [[nodiscard]] WGPUTextureFormat format() const override { return mFormat; }
    void resize(std::uint32_t width, std::uint32_t height) override;

    /** Skips the frame before the first resize, or if the surface had to be reconfigured. */
    [[nodiscard]] std::optional<Image> acquire() override;

    /** Native only: the browser presents canvases on its own. */
    void present() override;

private:
    Onscreen(Context& context, Handle<WGPUSurface> surface, WGPUTextureFormat format) noexcept;

    void configure();

    Handle<WGPUSurface> mSurface;
    WGPUTextureFormat mFormat;
    std::uint32_t mWidth = 0;
    std::uint32_t mHeight = 0;
};

#ifndef __EMSCRIPTEN__
/**
 * A texture we own, tracked in Context::resources. For headless tests:
 * needs no display, and its pixels can be copied back.
 */
class Surface::Offscreen final : public Surface
{
public:
    [[nodiscard]] static std::expected<std::unique_ptr<Offscreen>, std::string>
    create(Context& context, Type::Target::Offscreen size);

    [[nodiscard]] WGPUTextureFormat format() const override { return Format; }
    void resize(std::uint32_t width, std::uint32_t height) override;

    /** A view of the texture. Never skips a frame. */
    [[nodiscard]] std::optional<Image> acquire() override;

    /** Nothing to show. */
    void present() override {}

    /** The texture frames are drawn into, e.g. to copy pixels back. */
    [[nodiscard]] WGPUTexture texture() const { return mTexture.get(); }

private:
    // Readable back to the CPU without swizzling, for image tests.
    static constexpr WGPUTextureFormat Format = WGPUTextureFormat_RGBA8Unorm;
    static constexpr std::uint64_t BytesPerPixel = 4;

    explicit Offscreen(Context& context) noexcept;

    Handle<WGPUTexture> mTexture;
    std::uint32_t mWidth = 0;
    std::uint32_t mHeight = 0;
};
#endif

} // namespace Gpu
