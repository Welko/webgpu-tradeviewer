// The wasm module's start and what it exports to JavaScript: the `Viewer`
// class (Bridge.hpp) and a few functions. What only test pages use is bound
// in Testing.cpp.

#include "Web/Bridge.hpp"
#include "Web/Convert.hpp"

#include "Core/Log.hpp"

#include <emscripten/bind.h>
#include <emscripten/em_js.h>
#include <emscripten/heap.h>
#include <emscripten/val.h>

#include <new>
#include <string>

/*
 * Stops the module with a message saying why, which Module.onAbort receives.
 * A request fails when it would take the heap past its most, which it may
 * be well short of.
 */
// clang-format off: JavaScript, which clang-format would break.
EM_JS(void, viewerAbortOutOfMemory, (double heapBytes, double maxBytes), {
    abort("Out of memory: the viewer had " + Math.round(heapBytes / 1048576) + " MiB, and needed more than the " +
          Math.round(maxBytes / 1048576) + " MiB a page can have");
});
// clang-format on

namespace {

/** Lets JavaScript tell running out of memory from other aborts. Exceptions are off, so new can only abort. */
const bool OutOfMemoryHandled = [] {
    std::set_new_handler(
        [] { viewerAbortOutOfMemory(static_cast<double>(emscripten_get_heap_size()), static_cast<double>(emscripten_get_heap_max())); });
    return true;
}();

/** Calls `onError(message)` with every error the viewer logs, from now on, so the page can show it. */
void setErrorListener(emscripten::val onError)
{
    Core::Log::onError = [onError](const std::string& message) { onError(message); };
}

}  // namespace

EMSCRIPTEN_BINDINGS(Viewer)
{
    using Viewer::Web::Bridge;

    emscripten::value_object<Gpu::Type::Info>("GpuInfo")
        .field("vendor", &Gpu::Type::Info::vendor)
        .field("device", &Gpu::Type::Info::device)
        .field("architecture", &Gpu::Type::Info::architecture)
        .field("description", &Gpu::Type::Info::description)
        .field("backend", &Gpu::Type::Info::backend)
        .field("adapter", &Gpu::Type::Info::adapter)
        .field("webglRenderer", &Gpu::Type::Info::webglRenderer);

    emscripten::value_object<Viewer::Web::TradeStats>("TradeStats")
        .field("count", &Viewer::Web::TradeStats::count)
        .field("firstMs", &Viewer::Web::TradeStats::firstMs)
        .field("lastMs", &Viewer::Web::TradeStats::lastMs)
        .field("totalAmount", &Viewer::Web::TradeStats::totalAmount);

    emscripten::value_object<Viewer::Web::TimeRange>("TimeRange")
        .field("startMs", &Viewer::Web::TimeRange::startMs)
        .field("endMs", &Viewer::Web::TimeRange::endMs);

    emscripten::class_<Bridge>("Viewer")
        .constructor<>()
        .function("init", &Bridge::init)
        .function("info", &Bridge::getInfo)
        .function("resize", &Bridge::resize)
        .function("update", &Bridge::update)
        .function("needsRender", &Bridge::needsRender)
        .function("render", &Bridge::render)
        .function("view", &Bridge::getView)
        .function("viewStats", &Bridge::getViewStats)
        .function("axes", &Bridge::getAxes)
        .function("legend", &Bridge::getLegend)
        .function("setTheme", &Bridge::setTheme)
        .function("setPointer", &Bridge::setPointer)
        .function("clearPointer", &Bridge::clearPointer)
        .function("pickedTrade", &Bridge::getPickedTrade)
        .function("pickedColumn", &Bridge::getPickedColumn)
        .function("summaries", &Bridge::getSummaries)
        .function("setSummariesAllowed", &Bridge::setSummariesAllowed)
        .function("setSummaryThreshold", &Bridge::setSummaryThreshold)
        .function("setPlotInsets", &Bridge::setPlotInsets)
        .function("resetView", &Bridge::resetView)
        .function("showDay", &Bridge::showDay)
        .function("zoomAt", &Bridge::zoomAt)
        .function("panBy", &Bridge::panBy)
        .function("setMarkerScale", &Bridge::setMarkerScale)
        .function("setPixelRatio", &Bridge::setPixelRatio)
        .function("tradeStats", &Bridge::getTradeStats)
        .function("instrument", &Bridge::getInstrument)
        .function("setTelemetry", &Bridge::setTelemetry)
        .function("flushTelemetry", &Bridge::flushTelemetry)
        .function("telemetryProperties", &Bridge::getTelemetryProperties)
        .function("beginLoad", &Bridge::beginLoad)
        .function("appendChunk", &Bridge::appendChunk)
        .function("finishLoad", &Bridge::finishLoad)
        .function("showScene", &Bridge::showScene)
        .function("readPixels", &Bridge::readPixels)
        .function("gpuHealth", &Bridge::getGpuHealth)
        .function("memoryStats", &Bridge::getMemoryStats)
        .function("setWakeCallback", &Bridge::setWakeCallback)
        .function("setProfiling", &Bridge::setProfiling)
        .function("setStatsCallback", &Bridge::setStatsCallback);

    emscripten::function("setErrorListener", &setErrorListener);
}
