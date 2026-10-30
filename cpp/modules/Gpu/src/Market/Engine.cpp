#include "Gpu/Market/Engine.hpp"

#include "Gpu/Context.hpp"
#include "Gpu/Convert.hpp"
#include "Gpu/Handle.hpp"
#include "Gpu/Market/TradesPass.hpp"
#include "Gpu/Plot/Area.hpp"
#include "Gpu/Plot/GridPass.hpp"
#include "Gpu/Profiler.hpp"
#include "Gpu/Readback.hpp"
#include "Gpu/Resource.hpp"

#include "Core/Log.hpp"
#include "Core/Profiling.hpp"

#include <webgpu/webgpu.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace Gpu::Market {

namespace {

// Readable back to the CPU without swizzling, for image tests.
constexpr WGPUTextureFormat OffscreenFormat = WGPUTextureFormat_RGBA8Unorm;
constexpr std::uint64_t OffscreenBytesPerPixel = 4;

// Frames whose GPU times may be on their way at once.
constexpr std::size_t MaxWaitingFrames = 16;

/**
 * The first format that stores colours as given. Theme colours are sRGB
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
WGPUSurface createSurface(WGPUInstance instance, const Gpu::Type::Target::Any& target)
{
    WGPUSurfaceDescriptor descriptor{};

#ifdef __EMSCRIPTEN__
    const auto& canvas = std::get<Gpu::Type::Target::Canvas>(target);
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source{};
    source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    source.selector = Convert::ToWgpu::stringView(canvas.selector);
    descriptor.nextInChain = &source.chain;
    return wgpuInstanceCreateSurface(instance, &descriptor);
#else
    if (const auto* wayland = std::get_if<Gpu::Type::Target::Wayland>(&target)) {
        WGPUSurfaceSourceWaylandSurface source{};
        source.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        source.display = wayland->display;
        source.surface = wayland->surface;
        descriptor.nextInChain = &source.chain;
        return wgpuInstanceCreateSurface(instance, &descriptor);
    }
    if (const auto* xlib = std::get_if<Gpu::Type::Target::Xlib>(&target)) {
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

/**
 * Everything behind Engine. Frames go to a surface (window or canvas) or an
 * offscreen texture; acquire() and present() hide the difference.
 */
struct Engine::Impl
{
    /** The texture one frame draws into. */
    struct Image
    {
        Handle<WGPUTexture> texture;
        Handle<WGPUTextureView> view;
    };

    ~Impl();

    /** Creates the surface for a window or canvas, or records an offscreen size. */
    [[nodiscard]] std::expected<void, std::string> prepare(WGPUInstance instance, const Gpu::Type::Target::Any& target);

    /** Picks the format and makes the output ready, once the device exists. */
    [[nodiscard]] std::expected<void, std::string> finishOutput();

    /** Configures the surface, or recreates the offscreen texture. */
    void configure();

    void resize(std::uint32_t newWidth, std::uint32_t newHeight);

    /** This frame's image, or nothing to skip it (zero size, or the surface was reconfigured). */
    [[nodiscard]] std::optional<Image> acquire();

    /** Shows the frame. Only native windows need it. */
    void present();

    void render();

    /** Where frames go: a surface (window or canvas) or an offscreen texture. */
    struct Output
    {
        Handle<WGPUSurface> surface;  // set for a window or canvas
        Handle<WGPUTexture> offscreen;  // set for an offscreen target
        WGPUTextureFormat format = WGPUTextureFormat_Undefined;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    /** Frame timing: the profiler, and the frames waiting for its results. */
    struct Profiling
    {
        Profiler::StatsQueue statsQueue{ MaxWaitingFrames };
        std::shared_ptr<Profiler> profiler;  // set once the device exists
        bool enabled = false;
        std::uint64_t lastFrame = 0;  // frames drawn so far
    };

    /** In the order they draw. */
    struct Passes
    {
        Plot::GridPass grid;
        TradesPass trades;
    };

    // Declared first so it is destroyed last, after every tracked handle.
    Resource::Stats resources;

    // Destroyed in reverse order: the passes, the output, the profiler, then the context they use.
    Context context;
    Profiling profiling;
    Output output;
    std::optional<Passes> passes;  // set once the output format is known

    Plot::Type::Insets insets{};
    double nowMs = 0.0;  // the time frames show, for time-based effects
    bool dirty = true;
};

// Creation

void Engine::create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, CreatedCallback onDone)
{
    auto impl = std::make_unique<Impl>();
    Handle instance(wgpuCreateInstance(nullptr));
    if (!instance) {
        onDone(std::unexpected("Failed to create a WebGPU instance"));
        return;
    }
    if (auto prepared = impl->prepare(instance.get(), target); !prepared) {
        onDone(std::unexpected(std::move(prepared.error())));
        return;
    }

    // Shared, because the callback must be copyable; freed even if it never runs.
    struct Pending
    {
        std::unique_ptr<Impl> impl;
        CreatedCallback onDone;
    };
    const WGPUSurface compatible = impl->output.surface.get();  // null offscreen: any adapter
    auto pending = std::make_shared<Pending>(std::move(impl), std::move(onDone));

    Context::create(std::move(instance), compatible, powerPreference, [pending](Context::Result context) {
        auto impl = std::move(pending->impl);
        auto onDone = std::move(pending->onDone);
        if (!context) {
            onDone(std::unexpected(std::move(context.error())));
            return;
        }
        impl->context = std::move(*context);
        if (auto finished = impl->finishOutput(); !finished) {
            onDone(std::unexpected(std::move(finished.error())));
            return;
        }
        impl->passes.emplace(Impl::Passes{
            .grid = Plot::GridPass(impl->context, impl->resources, impl->output.format),
            .trades = TradesPass(impl->context, impl->resources, impl->output.format),
        });
        impl->profiling.profiler = Profiler::create(impl->context,
            [&statsQueue = impl->profiling.statsQueue](std::uint64_t frame, Profiler::Times times) {
                statsQueue.setTimes(frame, std::move(times));
            });
        onDone(std::unique_ptr<Engine>(new Engine(std::move(impl))));
    });
}

std::expected<void, std::string> Engine::Impl::prepare(WGPUInstance instance, const Gpu::Type::Target::Any& target)
{
#ifndef __EMSCRIPTEN__
    if (const auto* size = std::get_if<Gpu::Type::Target::Offscreen>(&target)) {
        if (size->width == 0 || size->height == 0) {
            return std::unexpected("An offscreen target needs a non-zero size");
        }
        output.width = size->width;
        output.height = size->height;
        return {};
    }
#endif
    // Created before the adapter, so the adapter can be chosen to match it.
    output.surface = Handle(createSurface(instance, target));
    if (!output.surface) {
        return std::unexpected("Failed to create a WebGPU surface");
    }
    return {};
}

std::expected<void, std::string> Engine::Impl::finishOutput()
{
    if (!output.surface) {
        // Offscreen: the size is known already, so the texture can exist now.
        output.format = OffscreenFormat;
        configure();
        return {};
    }

    // An empty list means this adapter can't draw to the surface at all.
    WGPUSurfaceCapabilities capabilities{};
    if (wgpuSurfaceGetCapabilities(output.surface.get(), context.adapter.get(), &capabilities) != WGPUStatus_Success
        || capabilities.formatCount == 0) {
        return std::unexpected("The WebGPU surface reports no supported formats");
    }
    output.format = pickSurfaceFormat(std::span(capabilities.formats, capabilities.formatCount));
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    // Unconfigured until the first resize().
    return {};
}

Engine::Impl::~Impl()
{
    // The handles release themselves afterwards; only the surface
    // configuration needs undoing first.
    if (output.surface && output.width > 0) {
        wgpuSurfaceUnconfigure(output.surface.get());
    }
}

// Output

void Engine::Impl::configure()
{
    if (!output.surface) {
        WGPUTextureDescriptor descriptor{};
        descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = { output.width, output.height, 1 };
        descriptor.format = output.format;
        descriptor.mipLevelCount = 1;
        descriptor.sampleCount = 1;
        const std::uint64_t bytes = std::uint64_t{ output.width } * output.height * OffscreenBytesPerPixel;
        output.offscreen = Handle<WGPUTexture>(wgpuDeviceCreateTexture(context.device.get(), &descriptor), resources, bytes);
        return;
    }

    WGPUSurfaceConfiguration configuration{};
    configuration.device = context.device.get();
    configuration.format = output.format;
    configuration.usage = WGPUTextureUsage_RenderAttachment;
    configuration.width = output.width;
    configuration.height = output.height;
    configuration.alphaMode = WGPUCompositeAlphaMode_Auto;
    configuration.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(output.surface.get(), &configuration);
}

void Engine::Impl::resize(std::uint32_t newWidth, std::uint32_t newHeight)
{
    if (newWidth == 0 || newHeight == 0 || (newWidth == output.width && newHeight == output.height)) {
        return;
    }
    output.width = newWidth;
    output.height = newHeight;
    configure();
    dirty = true;
}

std::optional<Engine::Impl::Image> Engine::Impl::acquire()
{
    if (output.width == 0 || output.height == 0) {
        return std::nullopt;
    }
    if (!output.surface) {
        return Image{ .texture = {}, .view = Handle(wgpuTextureCreateView(output.offscreen.get(), nullptr)) };
    }

    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(output.surface.get(), &surfaceTexture);
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

void Engine::Impl::present()
{
#ifndef __EMSCRIPTEN__
    // The browser presents the canvas on its own, and offscreen there is
    // nothing to present.
    if (output.surface) {
        wgpuSurfacePresent(output.surface.get());
    }
#endif
}

// Frames

void Engine::Impl::render()
{
    const bool timed = Core::Profiling::Compiled && profiling.enabled;
    double acquireMs = 0.0;
    double encodeMs = 0.0;
    double submitMs = 0.0;

    auto image = [&] {
        const Core::Profiling::Scope scope(acquireMs, timed);
        return acquire();
    }();
    if (!image) {
        // Skipped frames stay needed.
        return;
    }

    profiling.profiler->beginFrame(timed);
    const Handle encoder(wgpuDeviceCreateCommandEncoder(context.device.get(), nullptr));
    {
        const Core::Profiling::Scope scope(encodeMs, timed);
        const auto plot = Plot::Area::within(output.width, output.height, insets);
        passes->grid.encode(encoder.get(), image->view.get(), plot, *profiling.profiler);
        passes->trades.encode(encoder.get(), image->view.get(), plot, *profiling.profiler);
        profiling.profiler->endPasses(encoder.get());
    }
    {
        const Core::Profiling::Scope scope(submitMs, timed);
        const Handle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
        const WGPUCommandBuffer submitted = commands.get();
        wgpuQueueSubmit(context.queue.get(), 1, &submitted);
        present();
    }
    dirty = false;

    profiling.statsQueue.push({
        .frame = ++profiling.lastFrame,
        .cpu = timed ? std::optional(Gpu::Type::FrameStats::Cpu{ .acquireMs = acquireMs, .encodeMs = encodeMs, .submitMs = submitMs })
                     : std::nullopt,
        .gpu = std::unexpected(Gpu::Type::FrameStats::UntimedReason::Undefined),
        .buffers = resources.getCount(Resource::Kind::Buffer),
        .textures = resources.getCount(Resource::Kind::Texture),
        .gpuBytes = resources.getTotalBytes(),
    });
    profiling.profiler->endFrame(profiling.lastFrame);
}

// Public interface

Engine::Engine(std::unique_ptr<Impl> impl) noexcept
    : mImpl(std::move(impl))
{}

Engine::~Engine() = default;

const Gpu::Type::Info& Engine::getInfo() const
{
    return mImpl->context.info;
}

void Engine::resize(std::uint32_t width, std::uint32_t height)
{
    mImpl->resize(width, height);
}

void Engine::setPixelRatio(float ratio)
{
    mImpl->passes->trades.setPixelRatio(ratio);
    mImpl->dirty = true;
}

void Engine::setPlotInsets(Plot::Type::Insets insets)
{
    mImpl->insets = insets;
    mImpl->dirty = true;
}

void Engine::setGrid(const Plot::Type::Grid& grid)
{
    mImpl->passes->grid.setGrid(grid);
    mImpl->dirty = true;
}

void Engine::setTrades(const Core::Data::Trades& trades)
{
    mImpl->passes->trades.clear();
    mImpl->passes->trades.reserve(trades.size());
    mImpl->passes->trades.append(trades);
    mImpl->dirty = true;
}

void Engine::appendTrades(const Core::Data::Trades& trades)
{
    mImpl->passes->trades.append(trades);
    mImpl->dirty = true;
}

void Engine::reserveTrades(std::size_t count)
{
    mImpl->passes->trades.reserve(count);
}

void Engine::setView(const Type::View& view)
{
    mImpl->passes->trades.setView(view);
    mImpl->dirty = true;
}

void Engine::setMarkerScale(Type::MarkerScale scale)
{
    mImpl->passes->trades.setMarkerScale(scale);
    mImpl->dirty = true;
}

void Engine::setTheme(const Type::Theme& theme)
{
    mImpl->passes->grid.setTheme(theme.plot);
    mImpl->passes->trades.setTheme(theme);
    mImpl->dirty = true;
}

void Engine::update(double nowMs)
{
    mImpl->nowMs = nowMs;
}

bool Engine::needsRender() const
{
    return mImpl->dirty;
}

void Engine::render()
{
    mImpl->render();
}

void Engine::setProfiling(bool enabled)
{
    mImpl->profiling.enabled = enabled;
}

void Engine::setStatsCallback(const StatsCallback& onStats)
{
    mImpl->profiling.statsQueue.setCallback(onStats);
}

#ifndef __EMSCRIPTEN__
void Engine::processEvents()
{
    wgpuInstanceProcessEvents(mImpl->context.instance.get());
}

void Engine::readPixels(PixelsCallback onDone)
{
    if (!mImpl->output.offscreen) {
        onDone(std::unexpected("Only offscreen targets can be read back"));
        return;
    }
    Readback::texture(mImpl->context, mImpl->output.offscreen.get(), mImpl->output.width, mImpl->output.height, std::move(onDone));
}
#endif

} // namespace Gpu::Market
