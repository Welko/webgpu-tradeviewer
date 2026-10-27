#include "Gpu/Market/Engine.hpp"

#include "Gpu/Convert.hpp"
#include "Gpu/Handles.hpp"
#include "Gpu/Resource.hpp"

#include "Core/Log.hpp"
#include "Core/Profile.hpp"

#include <webgpu/webgpu.h>

#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>

EM_JS_DEPS(GpuEngine, "$stringToNewUTF8");

/* Returns a malloc'd UTF-8 string, or 0. Chrome masks gl.RENDERER as "WebKit WebGL". */
EM_JS(char*, gpuQueryWebglRenderer, (), {
    const gl = document.createElement("canvas").getContext("webgl");
    if (!gl) {
        return 0;
    }
    try {
        let renderer = gl.getParameter(gl.RENDERER);
        if (renderer === "WebKit WebGL") {
            const debugInfo = gl.getExtension("WEBGL_debug_renderer_info");
            renderer = debugInfo ? gl.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL) : null;
        }
        return (typeof renderer === "string" && renderer !== "") ? stringToNewUTF8(renderer) : 0;
    } finally {
        const loseContext = gl.getExtension("WEBGL_lose_context");
        if (loseContext) {
            loseContext.loseContext();
        }
    }
});
#endif

namespace Gpu::Market {

namespace {

constexpr WGPUColor ClearColor{ 0.07, 0.08, 0.10, 1.0 };

// Readable back to the CPU without swizzling, for image tests.
constexpr WGPUTextureFormat OffscreenFormat = WGPUTextureFormat_RGBA8Unorm;
constexpr std::uint64_t OffscreenBytesPerPixel = 4;

std::string readWebglRenderer()
{
#ifdef __EMSCRIPTEN__
    char* renderer = gpuQueryWebglRenderer();
    if (!renderer) {
        return {};
    }
    std::string result(renderer);
    std::free(renderer);
    return result;
#else
    return {};
#endif
}

Type::Info readInfo(WGPUAdapter adapter)
{
    WGPUAdapterInfo info{};
    if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) {
        Core::Log::warn("Failed to get WebGPU adapter info");
        Type::Info result;
        result.webglRenderer = readWebglRenderer();
        return result;
    }

    Type::Info result {
        .vendor = std::string(Convert::FromWgpu::stringView(info.vendor)),
        .device = std::string(Convert::FromWgpu::stringView(info.device)),
        .architecture = std::string(Convert::FromWgpu::stringView(info.architecture)),
        .description = std::string(Convert::FromWgpu::stringView(info.description)),
        .backend = std::string(Convert::FromWgpu::backend(info.backendType)),
        .adapter = std::string(Convert::FromWgpu::adapter(info.adapterType)),
        .webglRenderer = readWebglRenderer(),
    };
    wgpuAdapterInfoFreeMembers(info);
    return result;
}

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
 * Everything behind the public Engine: the WebGPU objects, where frames go,
 * and the passes. Frames go either to a surface (window or canvas) or, for
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

    /** Carried through the adapter and device callbacks as userdata. */
    struct Pending
    {
        std::unique_ptr<Impl> impl;
        Callback onDone;
    };

    static void fail(std::unique_ptr<Pending> pending, std::string_view what, WGPUStringView message = {});
    static void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                          void* userdata1, void*);
    static void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                         void* userdata1, void*);

    ~Impl();

    /** Creates the surface for a window or canvas, or records an offscreen size. */
    [[nodiscard]] std::expected<void, std::string> prepare(const Type::Target::Any& target);

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

    // Passes, in the order render() runs them.
    void encodeClear(WGPUCommandEncoder encoder, WGPUTextureView view) const;

    // Declared first so it is destroyed last, after every tracked handle.
    Resource::Stats resources;

    // Destroyed in reverse order.
    Handle<WGPUInstance> instance;
    Handle<WGPUSurface> surface;  // set for a window or canvas
    Handle<WGPUAdapter> adapter;
    Handle<WGPUDevice> device;
    Handle<WGPUQueue> queue;
    Handle<WGPUTexture> offscreen;  // set for an offscreen target

    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    /** Requested only when the adapter offers it, so GPU timing is optional. */
    bool hasTimestampQuery = false;
    Type::Info info;

    double nowMs = 0.0;  // the time frames show, for time-based effects
    bool dirty = true;
    bool profiling = false;
    Type::FrameStats stats;
};

// Creation

void Engine::create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone)
{
    auto pending = std::make_unique<Impl::Pending>(std::make_unique<Impl>(), std::move(onDone));
    auto& impl = *pending->impl;

    impl.instance = Handle(wgpuCreateInstance(nullptr));
    if (!impl.instance) {
        Impl::fail(std::move(pending), "Failed to create a WebGPU instance");
        return;
    }
    if (auto prepared = impl.prepare(target); !prepared) {
        Impl::fail(std::move(pending), prepared.error());
        return;
    }

    WGPURequestAdapterOptions options{};
    options.powerPreference = Convert::ToWgpu::powerPreference(powerPreference);
    options.compatibleSurface = impl.surface.get();  // null offscreen: any adapter

    wgpuInstanceRequestAdapter(impl.instance.get(), &options, WGPURequestAdapterCallbackInfo {
        .nextInChain = nullptr,
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = Impl::onAdapter,
        .userdata1 = pending.release(),
        .userdata2 = nullptr,
    });
}

void Engine::Impl::fail(std::unique_ptr<Pending> pending, std::string_view what, WGPUStringView message)
{
    const auto detail = Convert::FromWgpu::stringView(message);
    auto onDone = std::move(pending->onDone);
    pending.reset();
    onDone(std::unexpected(detail.empty() ? std::string(what) : std::format("{}: {}", what, detail)));
}

void Engine::Impl::onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                             void* userdata1, void*)
{
    std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata1));
    if (status != WGPURequestAdapterStatus_Success) {
        fail(std::move(pending), "No WebGPU adapter is available", message);
        return;
    }
    pending->impl->adapter = Handle(adapter);

    // Optional features are requested only when the adapter has them;
    // requesting a missing one fails the whole device request.
    std::vector<WGPUFeatureName> features;
    if (wgpuAdapterHasFeature(adapter, WGPUFeatureName_TimestampQuery)) {
        features.push_back(WGPUFeatureName_TimestampQuery);
    }

    WGPUDeviceDescriptor descriptor{};
    descriptor.requiredFeatureCount = features.size();
    descriptor.requiredFeatures = features.data();
    descriptor.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
            Core::Log::error("WebGPU error ({}): {}", static_cast<int>(type), Convert::FromWgpu::stringView(message));
        };

    wgpuAdapterRequestDevice(adapter, &descriptor, WGPURequestDeviceCallbackInfo {
        .nextInChain = nullptr,
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = onDevice,
        .userdata1 = pending.release(),
        .userdata2 = nullptr,
    });
}

void Engine::Impl::onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                            void* userdata1, void*)
{
    std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata1));
    if (status != WGPURequestDeviceStatus_Success) {
        fail(std::move(pending), "Failed to get a WebGPU device", message);
        return;
    }

    auto& impl = *pending->impl;
    impl.device = Handle(device);
    impl.queue = Handle(wgpuDeviceGetQueue(device));
    impl.hasTimestampQuery = wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery);
    if (auto finished = impl.finishOutput(); !finished) {
        fail(std::move(pending), finished.error());
        return;
    }
    impl.info = readInfo(impl.adapter.get());
    Core::Log::info("GPU timestamps: {}", impl.hasTimestampQuery ? "available" : "not available");

    auto engine = std::unique_ptr<Engine>(new Engine(std::move(pending->impl)));
    auto onDone = std::move(pending->onDone);
    pending.reset();
    onDone(std::move(engine));
}

std::expected<void, std::string> Engine::Impl::prepare(const Type::Target::Any& target)
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
    surface = Handle(createSurface(instance.get(), target));
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
    if (wgpuSurfaceGetCapabilities(surface.get(), adapter.get(), &capabilities) != WGPUStatus_Success
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
        offscreen = Handle<WGPUTexture>(wgpuDeviceCreateTexture(device.get(), &descriptor), resources, bytes);
        return;
    }

    WGPUSurfaceConfiguration configuration{};
    configuration.device = device.get();
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
    Type::FrameStats next;
    next.timed = Core::Profile::Compiled && profiling;

    auto image = [&] {
        const Core::Profile::Scope scope(next.cpuAcquireMs, next.timed);
        return acquire();
    }();
    if (!image) {
        // Skipped frames stay needed.
        return;
    }

    Handle<WGPUCommandEncoder> encoder;
    {
        const Core::Profile::Scope scope(next.cpuEncodeMs, next.timed);
        encoder = Handle(wgpuDeviceCreateCommandEncoder(device.get(), nullptr));
        encodeClear(encoder.get(), image->view.get());
    }
    {
        const Core::Profile::Scope scope(next.cpuSubmitMs, next.timed);
        const Handle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
        const WGPUCommandBuffer submitted = commands.get();
        wgpuQueueSubmit(queue.get(), 1, &submitted);
        present();
    }

    next.frame = stats.frame + 1;
    next.buffers = resources.count(Resource::Kind::Buffer);
    next.textures = resources.count(Resource::Kind::Texture);
    next.gpuBytes = resources.totalBytes();
    stats = next;
    dirty = false;
}

void Engine::Impl::encodeClear(WGPUCommandEncoder encoder, WGPUTextureView view) const
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
    return mImpl->info;
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

const Gpu::Type::FrameStats& Engine::stats() const
{
    return mImpl->stats;
}

} // namespace Gpu::Market
