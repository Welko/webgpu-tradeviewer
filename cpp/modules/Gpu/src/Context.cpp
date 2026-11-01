#include "Gpu/Context.hpp"

#include "Gpu/Convert.hpp"

#include "Core/Log.hpp"

#ifndef __EMSCRIPTEN__
#include <webgpu/wgpu.h>  // wgpuQueueGetTimestampPeriod
#endif

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

std::string_view describe(WGPUErrorType type)
{
    switch (type) {
    case WGPUErrorType_Validation:
        return "validation";
    case WGPUErrorType_OutOfMemory:
        return "out of memory";
    case WGPUErrorType_Internal:
        return "internal";
    default:
        return "unknown";
    }
}

void onUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void* userdata1, void*)
{
    auto& health = *static_cast<Context::Health*>(userdata1);
    if (health.lost) {
        // wgpu-native reports every call on a lost device; the loss says it all.
        return;
    }
    auto error = std::format("WebGPU {} error: {}", describe(type), Convert::FromWgpu::stringView(message));
    Core::Log::error("{}", error);
    if (health.errors.size() < Context::Health::KeptErrors) {
        health.errors.push_back(std::move(error));
    }
    ++health.errorCount;
}

void onDeviceLost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void* userdata1, void*)
{
    const std::unique_ptr<std::shared_ptr<Context::Health>> owned(static_cast<std::shared_ptr<Context::Health>*>(userdata1));
    auto& health = **owned;
    const auto detail = Convert::FromWgpu::stringView(message);
    health.lost = detail.empty() ? std::format("reason {}", static_cast<int>(reason)) : std::string(detail);
    if (health.released || reason == WGPUDeviceLostReason_FailedCreation || reason == WGPUDeviceLostReason_CallbackCancelled) {
        return;
    }
    Core::Log::warn("The GPU device was lost: {}", *health.lost);
    if (health.onLost) {
        health.onLost();
    }
}

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

    // The largest buffers the adapter allows, rather than the default 256 MiB:
    // the trades take 20 bytes each, all in one buffer. Other limits keep
    // their defaults.
    WGPULimits supported = WGPU_LIMITS_INIT;
    WGPULimits required = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(adapter, &supported) == WGPUStatus_Success) {
        required.maxBufferSize = supported.maxBufferSize;
    }

    // Owned by the lost callback, which the device calls exactly once, even
    // when it fails to be created or is released: errors can't come later.
    auto& health = pending->context.health;
    health = std::make_shared<Context::Health>();

    WGPUDeviceDescriptor descriptor{};
    descriptor.requiredFeatureCount = features.size();
    descriptor.requiredFeatures = features.data();
    descriptor.requiredLimits = &required;
    descriptor.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    descriptor.uncapturedErrorCallbackInfo.userdata1 = health.get();
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.callback = onDeviceLost;
    descriptor.deviceLostCallbackInfo.userdata1 = new std::shared_ptr<Context::Health>(health);

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
#ifndef __EMSCRIPTEN__
    context.nanosecondsPerTick = wgpuQueueGetTimestampPeriod(context.queue.get());
#endif
    context.info = readInfo(context.adapter.get());
    WGPULimits limits = WGPU_LIMITS_INIT;
    if (wgpuDeviceGetLimits(device, &limits) == WGPUStatus_Success) {
        context.maxBufferBytes = limits.maxBufferSize;
    }
    Core::Log::info("GPU timestamps: {}", context.features.timestampQuery
                                              ? std::format("available, {:.4g} ns per tick", context.nanosecondsPerTick)
                                              : "not available");

    auto result = std::move(pending->context);
    auto onDone = std::move(pending->onDone);
    pending.reset();
    onDone(std::move(result));
}

} // namespace

void Context::submit(WGPUCommandBuffer commands) const
{
    if (health && health->lost) {
        return;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    wgpuQueueSubmit(queue.get(), 1, &commands);
#pragma GCC diagnostic pop
}

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
