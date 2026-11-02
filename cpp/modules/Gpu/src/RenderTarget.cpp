#include "Gpu/RenderTarget.hpp"

#include "Gpu/Convert.hpp"

#include "Core/Log.hpp"
#include "Core/Profiling.hpp"

#include <algorithm>
#include <span>
#include <utility>
#include <variant>

namespace Gpu {

namespace {

// Readable back to the CPU without swizzling, for image tests.
constexpr WGPUTextureFormat OffscreenFormat = WGPUTextureFormat_RGBA8Unorm;
constexpr std::uint64_t OffscreenBytesPerPixel = 4;

/**
 * The first format that stores colors as given. Theme colors are sRGB
 * values, as CSS gives them; an sRGB format would convert them again and
 * draw them lighter than the page around them. Native surfaces often list
 * one first, browsers' canvases don't.
 */
WGPUTextureFormat pickSurfaceFormat(std::span<const WGPUTextureFormat> formats)
{
    const auto plain = std::ranges::find_if(formats, [](WGPUTextureFormat format) {
        return format != WGPUTextureFormat_RGBA8UnormSrgb && format != WGPUTextureFormat_BGRA8UnormSrgb;
    });
    return plain != formats.end() ? *plain : formats.front();
}

/** The WebGPU surface for a window or canvas; null if it can't be created. */
WGPUSurface createSurface(WGPUInstance instance, const Type::Target::Any& target)
{
#ifdef __EMSCRIPTEN__
    WGPUSurfaceDescriptor descriptor{};
    const auto& canvas = std::get<Type::Target::Canvas>(target);
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source{};
    source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    source.selector = Convert::ToWgpu::stringView(canvas.selector);
    descriptor.nextInChain = &source.chain;
    return wgpuInstanceCreateSurface(instance, &descriptor);
#elif defined(__linux__)
    WGPUSurfaceDescriptor descriptor{};
    if (const auto* wayland = std::get_if<Type::Target::Wayland>(&target)) {
        WGPUSurfaceSourceWaylandSurface source{};
        source.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        source.display = wayland->display;
        source.surface = wayland->surface;
        descriptor.nextInChain = &source.chain;
        return wgpuInstanceCreateSurface(instance, &descriptor);
    }
    if (const auto* xlib = std::get_if<Type::Target::Xlib>(&target)) {
        WGPUSurfaceSourceXlibWindow source{};
        source.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        source.display = xlib->display;
        source.window = xlib->window;
        descriptor.nextInChain = &source.chain;
        return wgpuInstanceCreateSurface(instance, &descriptor);
    }
    return nullptr;
#else
#error "createSurface() knows Linux's window systems and Web only"
#endif
}

} // namespace

RenderTarget::RenderTarget(const Context& context, Resource::Stats& resources) noexcept
    : mContext(&context),
      mResources(&resources)
{}

RenderTarget::~RenderTarget()
{
    // The handles release themselves afterwards; only the surface
    // configuration needs undoing first.
    if (mSurface && mWidthPx > 0) {
        wgpuSurfaceUnconfigure(mSurface.get());
    }
}

std::expected<RenderTarget::Prepared, std::string> RenderTarget::prepare(WGPUInstance instance, const Type::Target::Any& target)
{
    Prepared prepared;
#ifndef __EMSCRIPTEN__
    if (const auto* size = std::get_if<Type::Target::Offscreen>(&target)) {
        if (size->width == 0 || size->height == 0) {
            return std::unexpected("An offscreen target needs a non-zero size");
        }
        prepared.mWidthPx = size->width;
        prepared.mHeightPx = size->height;
        return prepared;
    }
#endif
    prepared.mSurface = Handle(createSurface(instance, target));
    if (!prepared.mSurface) {
        return std::unexpected("Failed to create a WebGPU surface");
    }
    return prepared;
}

std::expected<RenderTarget, std::string> RenderTarget::create(Prepared&& prepared, const Context& context, Resource::Stats& resources)
{
    RenderTarget target(context, resources);
    if (!prepared.mSurface) {
        // Offscreen: the size is known already, so the texture can exist now.
        target.mFormat = OffscreenFormat;
        target.mWidthPx = prepared.mWidthPx;
        target.mHeightPx = prepared.mHeightPx;
        target.configure();
        return target;
    }

    // An empty list means this adapter can't draw to the surface at all.
    WGPUSurfaceCapabilities capabilities{};
    if (wgpuSurfaceGetCapabilities(prepared.mSurface.get(), context.adapter.get(), &capabilities) != WGPUStatus_Success
        || capabilities.formatCount == 0) {
        return std::unexpected("The WebGPU surface reports no supported formats");
    }
    target.mSurface = std::move(prepared.mSurface);
    target.mFormat = pickSurfaceFormat(std::span(capabilities.formats, capabilities.formatCount));
#ifdef __EMSCRIPTEN__
    // A canvas takes any usage, though Emscripten's capabilities list none.
    target.mReadable = true;
#else
    target.mReadable = (capabilities.usages & WGPUTextureUsage_CopySrc) != 0;
#endif
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    // Unconfigured until the first size is set.
    return target;
}

void RenderTarget::configure()
{
    if (!mSurface) {
        WGPUTextureDescriptor descriptor{};
        descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = { mWidthPx, mHeightPx, 1 };
        descriptor.format = mFormat;
        descriptor.mipLevelCount = 1;
        descriptor.sampleCount = 1;
        const std::uint64_t bytes = std::uint64_t{ mWidthPx } * mHeightPx * OffscreenBytesPerPixel;
        mOffscreen = Handle<WGPUTexture>(wgpuDeviceCreateTexture(mContext->device.get(), &descriptor), *mResources, bytes);
        return;
    }

    WGPUSurfaceConfiguration configuration{};
    configuration.device = mContext->device.get();
    configuration.format = mFormat;
    configuration.usage = WGPUTextureUsage_RenderAttachment | (mCopied ? WGPUTextureUsage_CopySrc : WGPUTextureUsage_None);
    configuration.width = mWidthPx;
    configuration.height = mHeightPx;
    configuration.alphaMode = WGPUCompositeAlphaMode_Auto;
    configuration.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(mSurface.get(), &configuration);
}

void RenderTarget::setSize(std::uint32_t widthPx, std::uint32_t heightPx)
{
    if (widthPx == 0 || heightPx == 0 || (widthPx == mWidthPx && heightPx == mHeightPx)) {
        return;
    }
    mWidthPx = widthPx;
    mHeightPx = heightPx;
    configure();
}

std::optional<RenderTarget::Frame> RenderTarget::beginFrame(Type::FrameStats::Cpu& times, bool timed)
{
    auto frame = [&]() -> std::optional<Frame> {
        const Core::Profiling::Scope scope(times.acquireMs, timed);
        return acquire();
    }();
    if (frame) {
        frame->encoder = Handle(wgpuDeviceCreateCommandEncoder(mContext->device.get(), nullptr));
    }
    return frame;
}

std::optional<RenderTarget::Frame> RenderTarget::acquire()
{
    if (mWidthPx == 0 || mHeightPx == 0) {
        return std::nullopt;
    }
    if (!mSurface) {
        return Frame{ .texture = {}, .view = Handle(wgpuTextureCreateView(mOffscreen.get(), nullptr)), .encoder = {} };
    }

    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(mSurface.get(), &surfaceTexture);
    // The surface owns the texture's memory, so it isn't tracked.
    Handle<WGPUTexture> texture(surfaceTexture.texture);

    switch (surfaceTexture.status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        break;
    case WGPUSurfaceGetCurrentTextureStatus_Outdated:
    case WGPUSurfaceGetCurrentTextureStatus_Lost:
        configure();
        return std::nullopt;
    default:
        Core::Log::error("Failed to acquire the surface texture (status {})", static_cast<int>(surfaceTexture.status));
        return std::nullopt;
    }

    Handle view(wgpuTextureCreateView(texture.get(), nullptr));
    return Frame{ .texture = std::move(texture), .view = std::move(view), .encoder = {} };
}

bool RenderTarget::readPixels(Readback::Callback onDone)
{
    if (mOffscreen) {
        Readback::texture(*mContext, mOffscreen.get(), mFormat, mWidthPx, mHeightPx, std::move(onDone));
        return false;
    }
    if (!mReadable) {
        onDone(std::unexpected("This surface's frames can't be copied back"));
        return false;
    }
    if (!mCopied) {
        mCopied = true;
        // Unconfigured until the first size is set, which then configures it so.
        if (mWidthPx > 0) {
            configure();
        }
    }
    mPendingReads.push_back(std::move(onDone));
    return true;
}

void RenderTarget::endFrame(const Frame& frame, Type::FrameStats::Cpu& times, bool timed)
{
    const Core::Profiling::Scope scope(times.submitMs, timed);
    const Handle commands(wgpuCommandEncoderFinish(frame.encoder.get(), nullptr));
    mContext->submit(commands.get());
    // Before presenting: afterwards the texture's contents are gone.
    for (auto& onDone : std::exchange(mPendingReads, {})) {
        Readback::texture(*mContext, frame.texture.get(), mFormat, mWidthPx, mHeightPx, std::move(onDone));
    }
#ifndef __EMSCRIPTEN__
    // The browser presents the canvas on its own, and offscreen there is
    // nothing to present.
    if (mSurface) {
        wgpuSurfacePresent(mSurface.get());
    }
#endif
}

} // namespace Gpu
