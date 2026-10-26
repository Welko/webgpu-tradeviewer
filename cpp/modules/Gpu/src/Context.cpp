#include "Gpu/Context.hpp"

#include "Gpu/Convert.hpp"

#include "Core/Log.hpp"

#include <webgpu/webgpu.h>

#include <format>
#include <string_view>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>

#include <cstdlib>

EM_JS_DEPS(GpuContext, "$stringToNewUTF8");

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

namespace Gpu {

namespace {

/** Carried through the adapter and device callbacks as userdata. */
struct Pending {
    std::unique_ptr<Context> context;
    Context::Callback onDone;
};

void fail(std::unique_ptr<Pending> pending, std::string_view what, WGPUStringView message = {})
{
    const auto detail = Convert::FromWgpu::stringView(message);
    auto onDone = std::move(pending->onDone);
    pending.reset();
    onDone(std::unexpected(detail.empty() ? std::string(what) : std::format("{}: {}", what, detail)));
}

WGPUSurface createSurface(WGPUInstance instance, const Type::Target::Any& target)
{
    WGPUSurfaceDescriptor descriptor{};

#ifdef __EMSCRIPTEN__
    if (const auto* canvas = std::get_if<Type::Target::Canvas>(&target)) {
        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source{};
        source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        source.selector = Convert::ToWgpu::stringView(canvas->selector);
        descriptor.nextInChain = &source.chain;
        return wgpuInstanceCreateSurface(instance, &descriptor);
    }
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
#endif

    Core::Log::error("This surface target is not supported on this platform");
    return nullptr;
}

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

void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void*)
{
    std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata1));
    if (status != WGPURequestDeviceStatus_Success) {
        fail(std::move(pending), "Failed to get a WebGPU device", message);
        return;
    }

    auto& context = *pending->context;
    context.device = Handle(device);
    context.queue = Handle(wgpuDeviceGetQueue(device));

    WGPUSurfaceCapabilities capabilities{};
    if (wgpuSurfaceGetCapabilities(context.surface.get(), context.adapter.get(), &capabilities) != WGPUStatus_Success
        || capabilities.formatCount == 0) {
        fail(std::move(pending), "The WebGPU surface reports no supported formats");
        return;
    }
    context.format = capabilities.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    context.info = readInfo(context.adapter.get());

    auto result = std::move(pending->context);
    auto onDone = std::move(pending->onDone);
    pending.reset();
    onDone(std::move(result));
}

void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void*)
{
    std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata1));
    if (status != WGPURequestAdapterStatus_Success) {
        fail(std::move(pending), "No WebGPU adapter is available", message);
        return;
    }
    pending->context->adapter = Handle(adapter);

    WGPUDeviceDescriptor descriptor{};
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
} // namespace

void Context::create(const Type::Target::Any& target, Type::PowerPreference powerPreference, Callback onDone)
{
    auto pending = std::make_unique<Pending>(std::make_unique<Context>(), std::move(onDone));
    auto& context = *pending->context;

    context.instance = Handle(wgpuCreateInstance(nullptr));
    if (!context.instance) {
        fail(std::move(pending), "Failed to create a WebGPU instance");
        return;
    }

    context.surface = Handle(createSurface(context.instance.get(), target));
    if (!context.surface) {
        fail(std::move(pending), "Failed to create a WebGPU surface");
        return;
    }

    WGPURequestAdapterOptions options{};
    options.powerPreference = Convert::ToWgpu::powerPreference(powerPreference);
    options.compatibleSurface = context.surface.get();

    wgpuInstanceRequestAdapter(context.instance.get(), &options, WGPURequestAdapterCallbackInfo {
        .nextInChain = nullptr,
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = onAdapter,
        .userdata1 = pending.release(),
        .userdata2 = nullptr,
    });
}

Context::~Context()
{
    // The handles release themselves afterwards; only the surface
    // configuration needs undoing first.
    if (surface && width > 0) {
        wgpuSurfaceUnconfigure(surface.get());
    }
}

void Context::resize(std::uint32_t newWidth, std::uint32_t newHeight)
{
    if (newWidth == 0 || newHeight == 0 || (newWidth == width && newHeight == height)) {
        return;
    }
    width = newWidth;
    height = newHeight;
    configure();
}

void Context::configure()
{
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

} // namespace Gpu
