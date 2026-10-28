#include "Gpu/Market/Engine.hpp"

#include "Gpu/Context.hpp"
#include "Gpu/Convert.hpp"
#include "Gpu/Handles.hpp"
#include "Gpu/Profiler.hpp"
#include "Gpu/Readback.hpp"
#include "Gpu/Resource.hpp"

#include "Core/Log.hpp"
#include "Core/Profile.hpp"

#include <webgpu/webgpu.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace Gpu::Market {

namespace {

constexpr WGPUColor ClearColor{ 0.07, 0.08, 0.10, 1.0 };

// Readable back to the CPU without swizzling, for image tests.
constexpr WGPUTextureFormat OffscreenFormat = WGPUTextureFormat_RGBA8Unorm;
constexpr std::uint64_t OffscreenBytesPerPixel = 4;

/** The WebGPU surface for a window or canvas; null if it can't be created. */
WGPUSurface createSurface(WGPUInstance instance, const Type::Target::Any& target)
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

/**
 * Everything behind the public Engine: the Context, where frames go, and
 * the passes. Frames go either to a surface (window or canvas) or, for
 * Type::Target::Offscreen, to a texture the engine owns; acquire() and
 * present() hide the difference.
 */
struct Engine::Impl
{
    /** The texture one frame draws into. */
    struct Image
    {
        // The surface's current texture; empty when offscreen, since
        // `offscreen` keeps that texture.
        Handle<WGPUTexture> texture;
        Handle<WGPUTextureView> view;
    };

    ~Impl();

    /** Creates the surface for a window or canvas, or records an offscreen size. */
    [[nodiscard]] std::expected<void, std::string> prepare(WGPUInstance instance, const Type::Target::Any& target);

    /** Picks the format and makes the output ready, once the device exists. */
    [[nodiscard]] std::expected<void, std::string> finishOutput();

    /** Configures the surface, or recreates the offscreen texture. */
    void configure();

    void resize(std::uint32_t newWidth, std::uint32_t newHeight);

    /**
     * The image to draw this frame into, or nothing if the frame should be
     * skipped (zero size, or the surface had to be reconfigured).
     */
    [[nodiscard]] std::optional<Image> acquire();

    /** Shows the frame. Only native windows need it. */
    void present();

    void render();

    struct Passes
    {
        // Passes, in the order render() runs them. Each times itself with
        // `profiler`.
        static void encodeClear(WGPUCommandEncoder encoder, WGPUTextureView view, Profiler& profiler);
    };

    // Declared first so it is destroyed last, after every tracked handle.
    Resource::Stats resources;

    // Destroyed in reverse order: the output, then the context it uses.
    Context context;
    std::shared_ptr<Profiler> profiler;  // set once the device exists
    StatsCallback onStats;
    Handle<WGPUSurface> surface;  // set for a window or canvas
    Handle<WGPUTexture> offscreen;  // set for an offscreen target

    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    double nowMs = 0.0;  // the time frames show, for time-based effects
    bool dirty = true;
    bool profiling = false;
    std::uint64_t lastFrame = 0;  // frames drawn so far
};

// Creation

void Engine::create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, CreatedCallback onCreated)
{
    auto impl = std::make_unique<Impl>();
    Handle instance(wgpuCreateInstance(nullptr));
    if (!instance) {
        onCreated(std::unexpected("Failed to create a WebGPU instance"));
        return;
    }
    if (auto prepared = impl->prepare(instance.get(), target); !prepared) {
        onCreated(std::unexpected(std::move(prepared.error())));
        return;
    }

    // Shared, because the callback must be copyable; freed even if it never runs.
    struct Pending
    {
        std::unique_ptr<Impl> impl;
        CreatedCallback onCreated;
    };
    const WGPUSurface compatible = impl->surface.get();  // null offscreen: any adapter
    auto pending = std::make_shared<Pending>(std::move(impl), std::move(onCreated));

    Context::create(std::move(instance), compatible, powerPreference, [pending](Context::Result context) {
        auto impl = std::move(pending->impl);
        auto onCreated = std::move(pending->onCreated);
        if (!context) {
            onCreated(std::unexpected(std::move(context.error())));
            return;
        }
        impl->context = std::move(*context);
        if (auto finished = impl->finishOutput(); !finished) {
            onCreated(std::unexpected(std::move(finished.error())));
            return;
        }
        impl->profiler = Profiler::create(impl->context);
        onCreated(std::unique_ptr<Engine>(new Engine(std::move(impl))));
    });
}

std::expected<void, std::string> Engine::Impl::prepare(WGPUInstance instance, const Type::Target::Any& target)
{
#ifndef __EMSCRIPTEN__
    if (const auto* size = std::get_if<Type::Target::Offscreen>(&target)) {
        if (size->width == 0 || size->height == 0) {
            return std::unexpected("An offscreen target needs a non-zero size");
        }
        width = size->width;
        height = size->height;
        return {};
    }
#endif
    // Created before the adapter, so the adapter can be chosen to match it.
    surface = Handle(createSurface(instance, target));
    if (!surface) {
        return std::unexpected("Failed to create a WebGPU surface");
    }
    return {};
}

std::expected<void, std::string> Engine::Impl::finishOutput()
{
    if (!surface) {
        // Offscreen: the size is known already, so the texture can exist now.
        format = OffscreenFormat;
        configure();
        return {};
    }

    // An empty list means this adapter can't draw to the surface at all.
    WGPUSurfaceCapabilities capabilities{};
    if (wgpuSurfaceGetCapabilities(surface.get(), context.adapter.get(), &capabilities) != WGPUStatus_Success
        || capabilities.formatCount == 0) {
        return std::unexpected("The WebGPU surface reports no supported formats");
    }
    format = capabilities.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    // Unconfigured until the first resize().
    return {};
}

Engine::Impl::~Impl()
{
    // The handles release themselves afterwards; only the surface
    // configuration needs undoing first.
    if (surface && width > 0) {
        wgpuSurfaceUnconfigure(surface.get());
    }
}

// Output

void Engine::Impl::configure()
{
    if (!surface) {
        WGPUTextureDescriptor descriptor{};
        descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = { width, height, 1 };
        descriptor.format = format;
        descriptor.mipLevelCount = 1;
        descriptor.sampleCount = 1;
        const std::uint64_t bytes = std::uint64_t{ width } * height * OffscreenBytesPerPixel;
        offscreen = Handle<WGPUTexture>(wgpuDeviceCreateTexture(context.device.get(), &descriptor), resources, bytes);
        return;
    }

    WGPUSurfaceConfiguration configuration{};
    configuration.device = context.device.get();
    configuration.format = format;
    configuration.usage = WGPUTextureUsage_RenderAttachment;
    configuration.width = width;
    configuration.height = height;
    configuration.alphaMode = WGPUCompositeAlphaMode_Auto;
    configuration.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface.get(), &configuration);
}

void Engine::Impl::resize(std::uint32_t newWidth, std::uint32_t newHeight)
{
    if (newWidth == 0 || newHeight == 0 || (newWidth == width && newHeight == height)) {
        return;
    }
    width = newWidth;
    height = newHeight;
    configure();
    dirty = true;
}

std::optional<Engine::Impl::Image> Engine::Impl::acquire()
{
    if (width == 0 || height == 0) {
        return std::nullopt;
    }
    if (!surface) {
        return Image{ .texture = {}, .view = Handle(wgpuTextureCreateView(offscreen.get(), nullptr)) };
    }

    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(surface.get(), &surfaceTexture);
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
    if (surface) {
        wgpuSurfacePresent(surface.get());
    }
#endif
}

// Frames

void Engine::Impl::render()
{
    const bool timed = Core::Profile::Compiled && profiling;
    double acquireMs = 0.0;
    double encodeMs = 0.0;
    double submitMs = 0.0;

    auto image = [&] {
        const Core::Profile::Scope scope(acquireMs, timed);
        return acquire();
    }();
    if (!image) {
        // Skipped frames stay needed.
        return;
    }

    profiler->beginFrame(timed);
    const Handle encoder(wgpuDeviceCreateCommandEncoder(context.device.get(), nullptr));
    {
        const Core::Profile::Scope scope(encodeMs, timed);
        Passes::encodeClear(encoder.get(), image->view.get(), *profiler);
        profiler->endPasses(encoder.get());
    }
    {
        const Core::Profile::Scope scope(submitMs, timed);
        const Handle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
        const WGPUCommandBuffer submitted = commands.get();
        wgpuQueueSubmit(context.queue.get(), 1, &submitted);
        present();
    }
    dirty = false;

    auto stats = Type::FrameStats{
        .frame = ++lastFrame,
        .cpu = timed ? std::optional(Type::FrameStats::Cpu{ .acquireMs = acquireMs, .encodeMs = encodeMs, .submitMs = submitMs })
                     : std::nullopt,
        .gpu = std::unexpected(Type::FrameStats::UntimedReason::Undefined),
        .buffers = resources.count(Resource::Kind::Buffer),
        .textures = resources.count(Resource::Kind::Texture),
        .gpuBytes = resources.totalBytes(),
    };

    // The profiler is ours, so `this` outlives every call.
    profiler->endFrame([this, stats = std::move(stats)](Profiler::Times times) mutable {
        if (onStats) {
            stats.gpu = std::move(times);
            onStats(stats);
        }
    });
}

void Engine::Impl::Passes::encodeClear(WGPUCommandEncoder encoder, WGPUTextureView view, Profiler& profiler)
{
    WGPURenderPassColorAttachment color{};
    color.view = view;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = ClearColor;

    WGPURenderPassDescriptor descriptor{};
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    descriptor.timestampWrites = profiler.measurePass("clear");

    // Clearing happens when the pass begins; there is nothing to draw.
    const Handle pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));
    wgpuRenderPassEncoderEnd(pass.get());
}

// Public interface

Engine::Engine(std::unique_ptr<Impl> impl) noexcept
    : mImpl(std::move(impl))
{}

Engine::~Engine() = default;

const Gpu::Type::Info& Engine::info() const
{
    return mImpl->context.info;
}

void Engine::resize(std::uint32_t width, std::uint32_t height)
{
    mImpl->resize(width, height);
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
    mImpl->profiling = enabled;
}

void Engine::setStatsCallback(const StatsCallback& onStats)
{
    mImpl->onStats = onStats;
}

#ifndef __EMSCRIPTEN__
void Engine::processEvents()
{
    wgpuInstanceProcessEvents(mImpl->context.instance.get());
}

void Engine::readPixels(PixelsCallback onDone)
{
    if (!mImpl->offscreen) {
        onDone(std::unexpected("Only offscreen targets can be read back"));
        return;
    }
    Readback::texture(mImpl->context, mImpl->offscreen.get(), mImpl->width, mImpl->height, std::move(onDone));
}
#endif

} // namespace Gpu::Market
