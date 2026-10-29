#include "Gpu/Context.hpp"

#include "Gpu/Convert.hpp"

#include "Core/Log.hpp"

#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>

EM_JS_DEPS(GpuContext, "$stringToNewUTF8");

/*
 * Returns a malloc'd UTF-8 string, or 0. Chrome masks gl.RENDERER as "WebKit WebGL".
 * The context is left to the garbage collector: loseContext() would log "WebGL context was lost".
 */
EM_JS(char*, gpuQueryWebglRenderer, (), {
    const gl = document.createElement("canvas").getContext("webgl");
    if (!gl) {
        return 0;
    }
    let renderer = gl.getParameter(gl.RENDERER);
    if (renderer === "WebKit WebGL") {
        const debugInfo = gl.getExtension("WEBGL_debug_renderer_info");
        renderer = debugInfo ? gl.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL) : null;
    }
    return (typeof renderer === "string" && renderer !== "") ? stringToNewUTF8(renderer) : 0;
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

/** Read once per page, since each read creates a WebGL context. */
const std::string& getWebglRenderer()
{
    static const std::string renderer = [] {
#ifdef __EMSCRIPTEN__
        char* queried = gpuQueryWebglRenderer();
        if (!queried) {
            return std::string();
        }
        std::string result(queried);
        std::free(queried);
        return result;
#else
        return std::string();
#endif
    }();
    return renderer;
}

Type::Info readInfo(WGPUAdapter adapter)
{
    WGPUAdapterInfo info{};
    if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) {
        Core::Log::warn("Failed to get WebGPU adapter info");
        return Type::Info{
            .vendor = {},
            .device = {},
            .architecture = {},
            .description = {},
            .backend = {},
            .adapter = {},
            .webglRenderer = getWebglRenderer(),
        };
    }

    Type::Info result {
        .vendor = std::string(Convert::FromWgpu::stringView(info.vendor)),
        .device = std::string(Convert::FromWgpu::stringView(info.device)),
        .architecture = std::string(Convert::FromWgpu::stringView(info.architecture)),
        .description = std::string(Convert::FromWgpu::stringView(info.description)),
        .backend = std::string(Convert::FromWgpu::backend(info.backendType)),
        .adapter = std::string(Convert::FromWgpu::adapter(info.adapterType)),
        .webglRenderer = getWebglRenderer(),
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
    context.features = Context::Features{ .timestampQuery = wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery) != 0 };
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
#ifndef __EMSCRIPTEN__
    // Natively, events are pumped below until creation finishes, so onDone
    // runs before create() returns.
    constexpr auto Timeout = std::chrono::seconds(10);
    const auto finished = std::make_shared<bool>(false);
    onDone = [finished, onDone = std::move(onDone)](Result result) {
        if (!*finished) {
            *finished = true;
            onDone(std::move(result));
        }
    };
    // Our own reference: a failed creation releases the instance from inside
    // the pumping loop, which must not be the last reference.
    wgpuInstanceAddRef(instance.get());
    const Handle<WGPUInstance> pumped(instance.get());
    const Callback finish = onDone;
#endif

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

#ifndef __EMSCRIPTEN__
    const auto deadline = std::chrono::steady_clock::now() + Timeout;
    while (!*finished && std::chrono::steady_clock::now() < deadline) {
        wgpuInstanceProcessEvents(pumped.get());
        if (!*finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (!*finished) {
        finish(std::unexpected("Timed out waiting for the GPU to become ready"));
    }
#endif
}

} // namespace Gpu
