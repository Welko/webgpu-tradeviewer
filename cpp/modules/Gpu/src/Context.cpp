#include "Gpu/Context.hpp"

#include "Gpu/Convert.hpp"

#include "Core/Log.hpp"

#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>

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
struct Pending
{
    Context context;
    Context::Callback onDone;
};

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

void fail(std::unique_ptr<Pending> pending, std::string_view what, WGPUStringView message = {})
{
    const auto detail = Convert::FromWgpu::stringView(message);
    auto onDone = std::move(pending->onDone);
    pending.reset();
    onDone(std::unexpected(detail.empty() ? std::string(what) : std::format("{}: {}", what, detail)));
}

void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void*);

void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void*)
{
    std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata1));
    if (status != WGPURequestAdapterStatus_Success) {
        fail(std::move(pending), "No WebGPU adapter is available", message);
        return;
    }
    pending->context.adapter = Handle(adapter);

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

void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void*)
{
    std::unique_ptr<Pending> pending(static_cast<Pending*>(userdata1));
    if (status != WGPURequestDeviceStatus_Success) {
        fail(std::move(pending), "Failed to get a WebGPU device", message);
        return;
    }

    auto& context = pending->context;
    context.device = Handle(device);
    context.queue = Handle(wgpuDeviceGetQueue(device));
    context.features.timestampQuery = wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery);
    context.info = readInfo(context.adapter.get());
    Core::Log::info("GPU timestamps: {}", context.features.timestampQuery ? "available" : "not available");

    auto result = std::move(pending->context);
    auto onDone = std::move(pending->onDone);
    pending.reset();
    onDone(std::move(result));
}

} // namespace

void Context::create(Handle<WGPUInstance> instance, WGPUSurface compatible, Type::PowerPreference powerPreference,
                     Callback onDone)
{
    auto pending = std::make_unique<Pending>(Context{}, std::move(onDone));
    pending->context.instance = std::move(instance);

    WGPURequestAdapterOptions options{};
    options.powerPreference = Convert::ToWgpu::powerPreference(powerPreference);
    options.compatibleSurface = compatible;

    // Read before the call: argument order is unspecified, and one argument
    // releases `pending`.
    const WGPUInstance raw = pending->context.instance.get();
    wgpuInstanceRequestAdapter(raw, &options, WGPURequestAdapterCallbackInfo {
        .nextInChain = nullptr,
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback = onAdapter,
        .userdata1 = pending.release(),
        .userdata2 = nullptr,
    });
}

} // namespace Gpu
