#include "Gpu/Surface.hpp"

#include "Gpu/Convert.hpp"

#include "Core/Log.hpp"

#include <utility>
#include <variant>

namespace Gpu {

namespace {

/** Returns null on failure, or for Offscreen, which has no WebGPU surface. */
WGPUSurface createWgpuSurface(WGPUInstance instance, const Type::Target::Any& target)
{
    WGPUSurfaceDescriptor descriptor{};

#ifdef __EMSCRIPTEN__
    const auto& canvas = std::get<Type::Target::Canvas>(target);
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source{};
    source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    source.selector = Convert::ToWgpu::stringView(canvas.selector);
    descriptor.nextInChain = &source.chain;
    return wgpuInstanceCreateSurface(instance, &descriptor);
#else
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
#endif
}

} // namespace

Surface::Surface(Context& context) noexcept
    : mContext(context)
{
    mContext.resources.add(Resource::Kind::Surface, 0);
}

Surface::~Surface()
{
    mContext.resources.remove(Resource::Kind::Surface, 0);
}

std::expected<std::unique_ptr<Surface>, std::string> Surface::create(Context& context, const Type::Target::Any& target)
{
#ifndef __EMSCRIPTEN__
    if (const auto* size = std::get_if<Type::Target::Offscreen>(&target)) {
        return Offscreen::create(context, *size);
    }
#endif
    return Onscreen::create(context, target);
}

// Onscreen

std::expected<std::unique_ptr<Surface::Onscreen>, std::string>
Surface::Onscreen::create(Context& context, const Type::Target::Any& target)
{
    Handle surface(createWgpuSurface(context.instance.get(), target));
    if (!surface) {
        return std::unexpected("Failed to create a WebGPU surface");
    }

    // An empty list means this adapter can't draw to the surface at all.
    WGPUSurfaceCapabilities capabilities{};
    if (wgpuSurfaceGetCapabilities(surface.get(), context.adapter.get(), &capabilities) != WGPUStatus_Success
        || capabilities.formatCount == 0) {
        return std::unexpected("This GPU can't draw to the window or canvas; try another one");
    }
    const WGPUTextureFormat format = capabilities.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    return std::unique_ptr<Onscreen>(new Onscreen(context, std::move(surface), format));
}

Surface::Onscreen::Onscreen(Context& context, Handle<WGPUSurface> surface, WGPUTextureFormat format) noexcept
    : Surface(context),
      mSurface(std::move(surface)),
      mFormat(format)
{}

Surface::Onscreen::~Onscreen()
{
    // The handle releases the surface afterwards; only the configuration
    // needs undoing first.
    if (mWidth > 0) {
        wgpuSurfaceUnconfigure(mSurface.get());
    }
}

void Surface::Onscreen::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0 || (width == mWidth && height == mHeight)) {
        return;
    }
    mWidth = width;
    mHeight = height;
    configure();
}

void Surface::Onscreen::configure()
{
    WGPUSurfaceConfiguration configuration{};
    configuration.device = mContext.device.get();
    configuration.format = mFormat;
    configuration.usage = WGPUTextureUsage_RenderAttachment;
    configuration.width = mWidth;
    configuration.height = mHeight;
    configuration.alphaMode = WGPUCompositeAlphaMode_Auto;
    configuration.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(mSurface.get(), &configuration);
}

std::optional<Surface::Image> Surface::Onscreen::acquire()
{
    if (mWidth == 0) {
        return std::nullopt;
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
    return Image{ .texture = std::move(texture), .view = std::move(view) };
}

void Surface::Onscreen::present()
{
#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(mSurface.get());
#endif
}

#ifndef __EMSCRIPTEN__

// Offscreen

std::expected<std::unique_ptr<Surface::Offscreen>, std::string>
Surface::Offscreen::create(Context& context, Type::Target::Offscreen size)
{
    if (size.width == 0 || size.height == 0) {
        return std::unexpected("An offscreen target needs a non-zero size");
    }
    std::unique_ptr<Offscreen> result(new Offscreen(context));
    result->resize(size.width, size.height);
    if (!result->mTexture) {
        return std::unexpected("Failed to create the offscreen texture");
    }
    return result;
}

Surface::Offscreen::Offscreen(Context& context) noexcept
    : Surface(context)
{}

void Surface::Offscreen::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0 || (width == mWidth && height == mHeight)) {
        return;
    }
    mWidth = width;
    mHeight = height;

    WGPUTextureDescriptor descriptor{};
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = { width, height, 1 };
    descriptor.format = Format;
    descriptor.mipLevelCount = 1;
    descriptor.sampleCount = 1;
    const std::uint64_t bytes = std::uint64_t{ width } * height * BytesPerPixel;
    mTexture = Handle<WGPUTexture>(wgpuDeviceCreateTexture(mContext.device.get(), &descriptor), mContext.resources, bytes);
}

std::optional<Surface::Image> Surface::Offscreen::acquire()
{
    return Image{ .texture = {}, .view = Handle(wgpuTextureCreateView(mTexture.get(), nullptr)) };
}

#endif

} // namespace Gpu
