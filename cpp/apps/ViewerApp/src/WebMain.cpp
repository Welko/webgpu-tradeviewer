#include "Viewer/App.hpp"
#include "Viewer/Scene.hpp"

#include "Core/Memory.hpp"
#include "Core/Plot/Axis.hpp"
#include "Core/Data/TradeParser.hpp"

#include <emscripten/bind.h>
#include <emscripten/em_js.h>
#include <emscripten/heap.h>
#include <emscripten/val.h>

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/*
 * Stops the module with a message saying why, which Module.onAbort receives.
 * A request fails when it would take the heap past its most, which it may
 * be well short of.
 */
EM_JS(void, viewerAbortOutOfMemory, (double heapBytes, double maxBytes), {
    abort("Out of memory: the viewer had " + Math.round(heapBytes / 1048576) + " MiB, and needed more than the " +
          Math.round(maxBytes / 1048576) + " MiB a page can have");
});

namespace {

using emscripten::val;

/** Lets JavaScript tell running out of memory from other aborts. Exceptions are off, so new can only abort. */
const bool OutOfMemoryHandled = [] {
    std::set_new_handler([] {
        viewerAbortOutOfMemory(static_cast<double>(emscripten_get_heap_size()), static_cast<double>(emscripten_get_heap_max()));
    });
    return true;
}();

/** Accepts the WebGPU spelling: "low-power", "high-performance", anything else is the default. */
Gpu::Type::PowerPreference parsePowerPreference(const std::string& value)
{
    if (value == "low-power") {
        return Gpu::Type::PowerPreference::LowPower;
    }
    if (value == "high-performance") {
        return Gpu::Type::PowerPreference::HighPerformance;
    }
    return Gpu::Type::PowerPreference::Default;
}

/** Core::Data::TradeStats in numbers JavaScript reads exactly, with times in milliseconds for Date. */
struct WebTradeStats
{
    double count;
    double firstMs;
    double lastMs;
    double totalAmount;
};

/** Ticks as a JavaScript array of { fraction, label }. */
val toWeb(const std::vector<Core::Plot::Axis::Tick>& ticks)
{
    val array = val::array();
    for (const auto& tick : ticks) {
        val object = val::object();
        object.set("fraction", tick.fraction);
        object.set("label", tick.label);
        array.call<void>("push", object);
    }
    return array;
}

/** A color as a JavaScript { red, green, blue }, each from 0 to 1. */
val toWeb(Gpu::Type::Color color)
{
    val object = val::object();
    object.set("red", color.red);
    object.set("green", color.green);
    object.set("blue", color.blue);
    return object;
}

/**
 * A legend as { buy, sell, opacity, sizes: [{ amount, radiusCssPx, bound }] },
 * `bound` being "exact", "atMost" or "atLeast".
 */
val toWeb(const Gpu::Market::Type::Legend& legend)
{
    using Bound = Gpu::Market::Type::LegendSize::Bound;
    val sizes = val::array();
    for (const auto& size : legend.sizes) {
        val object = val::object();
        object.set("amount", size.amount);
        object.set("radiusCssPx", size.radiusCssPx);
        object.set("bound", size.bound == Bound::AtMost ? "atMost" : size.bound == Bound::AtLeast ? "atLeast" : "exact");
        sizes.call<void>("push", object);
    }
    val result = val::object();
    result.set("buy", toWeb(legend.buy));
    result.set("sell", toWeb(legend.sell));
    result.set("opacity", legend.opacity);
    result.set("sizes", sizes);
    return result;
}

/** A JavaScript { red, green, blue }, each from 0 to 1. */
Gpu::Type::Color toColor(const val& color)
{
    return { .red = color["red"].as<float>(), .green = color["green"].as<float>(), .blue = color["blue"].as<float>() };
}

WebTradeStats toWeb(const Core::Data::TradeStats& stats)
{
    return {
        .count = static_cast<double>(stats.count),
        .firstMs = static_cast<double>(stats.firstUs) / 1000.0,
        .lastMs = static_cast<double>(stats.lastUs) / 1000.0,
        .totalAmount = stats.totalAmount,
    };
}

/** The image-test scenes, as an array of { name, description, widthPx, heightPx }. */
val getScenes()
{
    val array = val::array();
    for (const auto& scene : Viewer::Scene::getScenes()) {
        val object = val::object();
        object.set("name", std::string(scene.name));
        object.set("description", std::string(scene.description));
        object.set("widthPx", scene.widthPx);
        object.set("heightPx", scene.heightPx);
        array.call<void>("push", object);
    }
    return array;
}

/** Known pixels as an array of { what, xPx, yPx, widthPx, heightPx, rgba, tolerance }. */
val toWeb(const std::vector<Viewer::Scene::Patch>& patches)
{
    val array = val::array();
    for (const auto& patch : patches) {
        val rgba = val::array();
        for (const std::uint8_t channel : patch.rgba) {
            rgba.call<void>("push", channel);
        }
        val object = val::object();
        object.set("what", patch.what);
        object.set("xPx", patch.xPx);
        object.set("yPx", patch.yPx);
        object.set("widthPx", patch.widthPx);
        object.set("heightPx", patch.heightPx);
        object.set("rgba", rgba);
        object.set("tolerance", patch.tolerance);
        array.call<void>("push", object);
    }
    return array;
}

/** Why a frame has no GPU times, as JavaScript reads it. */
const char* toWeb(Gpu::Type::FrameStats::UntimedReason reason)
{
    using Reason = Gpu::Type::FrameStats::UntimedReason;
    switch (reason) {
    case Reason::Undefined:
        return "undefined";
    case Reason::ProfilingOff:
        return "profilingOff";
    case Reason::Unsupported:
        return "unsupported";
    case Reason::NoFreeSlot:
        return "noFreeSlot";
    case Reason::NotStarted:
        return "notStarted";
    case Reason::ReadbackFailed:
        return "readbackFailed";
    case Reason::Overflow:
        return "overflow";
    }
    return "undefined";
}

/**
 * A frame's stats as { frame, cpu, gpu, untimed, buffers, textures, gpuBytes }:
 * `cpu` is { acquireMs, encodeMs, submitMs } or null; `gpu` is { totalMs,
 * passes: [{ name, gpuMs }] }, or null with `untimed` saying why.
 */
val toWeb(const Gpu::Type::FrameStats& stats)
{
    val object = val::object();
    object.set("frame", static_cast<double>(stats.frame));
    if (stats.cpu) {
        val cpu = val::object();
        cpu.set("acquireMs", stats.cpu->acquireMs);
        cpu.set("encodeMs", stats.cpu->encodeMs);
        cpu.set("submitMs", stats.cpu->submitMs);
        object.set("cpu", cpu);
    } else {
        object.set("cpu", val::null());
    }
    if (stats.gpu) {
        val passes = val::array();
        for (const auto& pass : stats.gpu->passes) {
            val entry = val::object();
            entry.set("name", pass.name);
            entry.set("gpuMs", pass.gpuMs);
            passes.call<void>("push", entry);
        }
        val gpu = val::object();
        gpu.set("totalMs", stats.gpu->totalMs);
        gpu.set("passes", passes);
        object.set("gpu", gpu);
        object.set("untimed", val::null());
    } else {
        object.set("gpu", val::null());
        object.set("untimed", toWeb(stats.gpu.error()));
    }
    object.set("buffers", stats.buffers);
    object.set("textures", stats.textures);
    object.set("gpuBytes", static_cast<double>(stats.gpuBytes));
    return object;
}

/** A time range in milliseconds since the epoch, for Date. */
struct WebTimeRange
{
    double startMs;
    double endMs;
};

/**
 * The object JavaScript holds. JavaScript owns the frame loop and must
 * call delete() when done.
 */
class WebViewer
{
public:
    /** Calls `onDone(error)` once: `null` on success, a string on failure. */
    void init(std::string selector, std::string powerPreference, val onDone)
    {
        std::weak_ptr<std::unique_ptr<Viewer::App>> slot = mApp;
        const Gpu::Market::Engine::Params params{
            .target = Gpu::Type::Target::Canvas{ .selector = std::move(selector) },
            .powerPreference = parsePowerPreference(powerPreference),
        };
        // The engine first, rather than App::create(), so Testing can reach it.
        Gpu::Market::Engine::create(params, [this, slot, params, onDone](Gpu::Market::Engine::CreatedResult result) {
            auto app = slot.lock();
            if (!app) {
                // delete() was called before initialization finished.
                return;
            }
            if (!result) {
                onDone(result.error());
                return;
            }
            // The slot lives as long as this viewer.
            mFirstEngine = result->get();
            *app = std::make_unique<Viewer::App>(std::move(*result), params);
            if (auto set = (*app)->setTrades(Viewer::generateSyntheticDay()); !set) {
                onDone(set.error());
                return;
            }
            onDone(val::null());
        });
    }

    /** The GPU in use, as a plain JavaScript object. Empty until init() succeeds. */
    Gpu::Type::Info getInfo() const
    {
        return *mApp ? (*mApp)->getInfo() : Gpu::Type::Info{};
    }

    void resize(std::uint32_t widthPx, std::uint32_t heightPx)
    {
        if (*mApp) {
            (*mApp)->resize(widthPx, heightPx);
        }
    }

    void update(double nowMs)
    {
        if (*mApp) {
            (*mApp)->update(nowMs);
        }
    }

    bool needsRender() const
    {
        return *mApp && (*mApp)->needsRender();
    }

    void render()
    {
        if (*mApp) {
            (*mApp)->render();
        }
    }

    /** The time range on screen. Empty until init() succeeds. */
    WebTimeRange getView() const
    {
        if (!*mApp) {
            return {};
        }
        const auto view = (*mApp)->getView();
        return { .startMs = static_cast<double>(view.startUs) / 1000.0, .endMs = static_cast<double>(view.endUs) / 1000.0 };
    }

    /** The trades in view. Empty until init() succeeds. */
    WebTradeStats getViewStats() const
    {
        return *mApp ? toWeb((*mApp)->getViewStats()) : WebTradeStats{};
    }

    /** The axes' ticks as { times, prices }, each an array of { fraction, label }. Null until init() succeeds. */
    val getAxes() const
    {
        if (!*mApp) {
            return val::null();
        }
        const auto& axes = (*mApp)->getAxes();
        val result = val::object();
        result.set("times", toWeb(axes.times));
        result.set("prices", toWeb(axes.prices));
        return result;
    }

    /** Takes { background, grid, buy, sell, cursor, markerOpacity }, each color a { red, green, blue } from 0 to 1. */
    void setTheme(const val& theme)
    {
        if (*mApp) {
            (*mApp)->setTheme({
                .plot = { .background = toColor(theme["background"]), .grid = toColor(theme["grid"]) },
                .buy = toColor(theme["buy"]),
                .sell = toColor(theme["sell"]),
                .markerOpacity = theme["markerOpacity"].as<float>(),
                .cursor = toColor(theme["cursor"]),
            });
        }
    }

    /** From the canvas's top left corner. */
    void setPointer(double xPx, double yPx)
    {
        if (*mApp) {
            (*mApp)->setPointer(xPx, yPx);
        }
    }

    void clearPointer()
    {
        if (*mApp) {
            (*mApp)->clearPointer();
        }
    }

    /**
     * The trade the cursor picks, as { timeUs, price, amount, side, centerXPx,
     * centerYPx }, or null. `side` is "buy" or "sell"; `centerXPx` and
     * `centerYPx` place its marker's center from the canvas's top left corner.
     */
    val getPickedTrade() const
    {
        const auto picked = *mApp ? (*mApp)->getPickedTrade() : std::nullopt;
        if (!picked) {
            return val::null();
        }
        val result = val::object();
        // Exact: microseconds since 1970 stay below 2^53 for another 250 years.
        result.set("timeUs", static_cast<double>(picked->timeUs));
        result.set("price", picked->price);
        result.set("amount", picked->amount);
        result.set("side", picked->side == Core::Data::Side::Buy ? "buy" : "sell");
        result.set("centerXPx", picked->centerXPx);
        result.set("centerYPx", picked->centerYPx);
        return result;
    }

    /**
     * The column summary the cursor picks, while summaries are shown, as
     * { startUs, endUs, count, buyCount, buyAmount, sellAmount, priceLow,
     * priceHigh, centerXPx, topPx, bottomPx }, or null. `centerXPx`, `topPx`
     * and `bottomPx` place its bar from the canvas's top left corner.
     */
    val getPickedColumn() const
    {
        const auto picked = *mApp ? (*mApp)->getPickedColumn() : std::nullopt;
        if (!picked) {
            return val::null();
        }
        const auto& summary = picked->summary;
        val result = val::object();
        result.set("startUs", static_cast<double>(picked->time.startUs));
        result.set("endUs", static_cast<double>(picked->time.endUs));
        result.set("count", static_cast<double>(summary.trades.size()));
        result.set("buyCount", static_cast<double>(summary.buyCount));
        result.set("buyAmount", summary.buyAmount);
        result.set("sellAmount", summary.sellAmount);
        result.set("priceLow", summary.price.low);
        result.set("priceHigh", summary.price.high);
        result.set("centerXPx", picked->centerXPx);
        result.set("topPx", picked->topPx);
        result.set("bottomPx", picked->bottomPx);
        return result;
    }

    /**
     * While the view shows column summaries, { across, withTrades }: how many
     * columns split the plot, and how many of those have trades (the bars
     * drawn). Otherwise null.
     */
    val getSummaries() const
    {
        if (!*mApp || (*mApp)->getColumnsAcross() == 0) {
            return val::null();
        }
        val result = val::object();
        result.set("across", (*mApp)->getColumnsAcross());
        result.set("withTrades", static_cast<double>((*mApp)->getColumns().size()));
        return result;
    }

    /** Whether crowded views may show column summaries (the default), or always every trade. */
    void setSummariesAllowed(bool allowed)
    {
        if (*mApp) {
            (*mApp)->setSummariesAllowed(allowed);
        }
    }

    /** Trades per CSS pixel column, on average, above which summaries are shown. 32 by default. */
    void setSummaryThreshold(double tradesPerColumn)
    {
        if (*mApp) {
            (*mApp)->setSummaryThreshold(tradesPerColumn);
        }
    }

    /** What markers look like at the current marker scale, for a legend; null before init() succeeds. */
    val getLegend() const
    {
        return *mApp ? toWeb((*mApp)->getLegend()) : val::null();
    }

    /** Space around the plot for axis labels. */
    void setPlotInsets(std::uint32_t leftPx, std::uint32_t topPx, std::uint32_t rightPx, std::uint32_t bottomPx)
    {
        if (*mApp) {
            (*mApp)->setPlotInsets({ .left = leftPx, .top = topPx, .right = rightPx, .bottom = bottomPx });
        }
    }

    void resetView()
    {
        if (*mApp) {
            (*mApp)->resetView();
        }
    }

    void showDay()
    {
        if (*mApp) {
            (*mApp)->showDay();
        }
    }

    /** `xPx` from the canvas's left edge; a `factor` above 1 zooms out. */
    void zoomAt(double xPx, double factor)
    {
        if (*mApp) {
            (*mApp)->zoomAt(xPx, factor);
        }
    }

    void panBy(double dxPx)
    {
        if (*mApp) {
            (*mApp)->panBy(dxPx);
        }
    }

    /** "area" or "log". */
    void setMarkerScale(const std::string& scale)
    {
        if (*mApp) {
            (*mApp)->setMarkerScale(scale == "log" ? Gpu::Market::Type::MarkerScale::Log : Gpu::Market::Type::MarkerScale::Area);
        }
    }

    void setPixelRatio(float ratio)
    {
        if (*mApp) {
            (*mApp)->setPixelRatio(ratio);
        }
    }

    /**
     * What the trades are in, as { base, quote, amountDecimals,
     * priceDecimals }: the assets' names, such as "BTC" and "USDT", and the
     * most decimals the file gives amounts and prices with. Null until
     * init() succeeds.
     */
    val getInstrument() const
    {
        if (!*mApp) {
            return val::null();
        }
        const auto& instrument = (*mApp)->getInstrument();
        val object = val::object();
        object.set("base", instrument.base);
        object.set("quote", instrument.quote);
        object.set("amountDecimals", instrument.amountDecimals);
        object.set("priceDecimals", instrument.priceDecimals);
        return object;
    }

    /** The trades on show. Empty until init() succeeds. */
    WebTradeStats getTradeStats() const
    {
        return *mApp ? toWeb(Core::Data::getStats((*mApp)->getTrades())) : WebTradeStats{};
    }

    /** How the GPU is doing, as { restarting, restarts, failure, errors, errorCount } (see Viewer::App::GpuHealth). Null until init() succeeds. */
    val getGpuHealth() const
    {
        if (!*mApp) {
            return val::null();
        }
        const auto health = (*mApp)->getGpuHealth();
        val errors = val::array();
        for (const auto& error : health.errors) {
            errors.call<void>("push", error);
        }
        val object = val::object();
        object.set("restarting", health.restarting);
        object.set("restarts", static_cast<double>(health.restarts));
        object.set("failure", health.failure ? val(*health.failure) : val::null());
        object.set("errors", errors);
        object.set("errorCount", static_cast<double>(health.errorCount));
        return object;
    }

    /**
     * What the app's large parts hold, as { tradesBytes, totalsBytes,
     * priceTreeBytes, summariesBytes, loadingBytes, gpuBytes, heapBytes } (see
     * Viewer::App::MemoryStats). Null until init() succeeds.
     */
    val getMemoryStats() const
    {
        if (!*mApp) {
            return val::null();
        }
        const auto memory = (*mApp)->getMemoryStats();
        val object = val::object();
        object.set("tradesBytes", static_cast<double>(memory.tradesBytes));
        object.set("totalsBytes", static_cast<double>(memory.totalsBytes));
        object.set("priceTreeBytes", static_cast<double>(memory.priceTreeBytes));
        object.set("summariesBytes", static_cast<double>(memory.summariesBytes));
        object.set("loadingBytes", static_cast<double>(mParser ? mParser->getBytes() : 0));
        object.set("gpuBytes", static_cast<double>(memory.gpuBytes));
        object.set("heapBytes", static_cast<double>(Core::Memory::getProcessBytes().value_or(0)));
        return object;
    }

    /** Calls `onWake()` when the viewer needs a frame without any input: when the GPU is lost, and once it's back. */
    void setWakeCallback(val onWake)
    {
        if (*mApp) {
            (*mApp)->setWakeCallback([onWake] { onWake(); });
        }
    }

    /** Turns frame timing on or off. Off by default. */
    void setProfiling(bool enabled)
    {
        if (*mApp) {
            (*mApp)->setProfiling(enabled);
        }
    }

    /**
     * Calls `onStats(stats)` once per drawn frame, in order, once its GPU
     * times are in (see toWeb()), or stops with null. It mustn't call render().
     */
    void setStatsCallback(val onStats)
    {
        if (!*mApp) {
            return;
        }
        if (onStats.isNull()) {
            (*mApp)->setStatsCallback(nullptr);
            return;
        }
        (*mApp)->setStatsCallback([onStats](const Gpu::Type::FrameStats& stats) { onStats(toWeb(stats)); });
    }

    /**
     * Starts reading a trades file, which appendChunk() then receives piece
     * by piece. Its name gives the assets when the file doesn't, as
     * Binance's don't: BTCUSDT-trades-2019-12-01.zip.
     */
    void beginLoad(const std::string& fileName)
    {
        mParser.emplace(fileName);
    }

    /** Parses a Uint8Array of the file. An error message, or "" if all is well. */
    std::string appendChunk(const val& bytes)
    {
        if (!mParser) {
            return "beginLoad() wasn't called";
        }
        mChunk.resize(bytes["length"].as<std::size_t>());
        val(emscripten::typed_memory_view(mChunk.size(), mChunk.data())).call<void>("set", bytes);
        auto appended = mParser->append(std::string_view(mChunk.data(), mChunk.size()));
        if (!appended) {
            mParser.reset();
            return appended.error();
        }
        return {};
    }

    /** Shows the file's trades in place of the current ones. An error message, or "" if all is well. */
    std::string finishLoad()
    {
        if (!mParser || !*mApp) {
            return "beginLoad() wasn't called";
        }
        auto trades = mParser->finish();
        mParser.reset();
        if (!trades) {
            return trades.error();
        }
        if (auto set = (*mApp)->setTrades(std::move(*trades)); !set) {
            return set.error();
        }
        return {};
    }

    /**
     * Shows the image-test scene called `name` (see Viewer::Scene) in place
     * of everything else. Its known pixels (see toWeb()), or null if there's
     * no such scene.
     */
    val showScene(const std::string& name)
    {
        const auto* scene = Viewer::Scene::find(name);
        if (!*mApp || !scene) {
            return val::null();
        }
        return toWeb(Viewer::Scene::show(*scene, **mApp));
    }

    /**
     * Copies the next frame drawn back, then calls `onDone(pixels, error)`:
     * a Uint8Array of RGBA rows, top row first, and null; or null and a message.
     */
    void readPixels(val onDone)
    {
        if (!*mApp) {
            onDone(val::null(), std::string("The viewer isn't ready"));
            return;
        }
        (*mApp)->readPixels([onDone](const std::expected<std::vector<std::uint8_t>, std::string>& pixels) {
            if (!pixels) {
                onDone(val::null(), pixels.error());
                return;
            }
            // A copy that outlives the vector.
            onDone(val(emscripten::typed_memory_view(pixels->size(), pixels->data())).call<val>("slice"), val::null());
        });
    }

private:
    friend struct Testing;

    std::shared_ptr<std::unique_ptr<Viewer::App>> mApp = std::make_shared<std::unique_ptr<Viewer::App>>();
    // The app's first engine, for Testing; gone once the app replaces it.
    Gpu::Market::Engine* mFirstEngine = nullptr;
    std::optional<Core::Data::TradeParser> mParser;  // while a file loads
    std::vector<char> mChunk;  // the last chunk, copied out of JavaScript
};

/**
 * Faults to inject, for the browser tests: not part of the viewer. They
 * ship in the published build, so the tests run what's published; each
 * harms only the page that calls it.
 */
struct Testing
{
    /** Loses `viewer`'s GPU device, as a driver reset would. False once its first engine is gone. */
    static bool loseGpu(WebViewer& viewer)
    {
        if (!*viewer.mApp || (*viewer.mApp)->getGpuHealth().restarts > 0) {
            return false;
        }
        viewer.mFirstEngine->loseDevice();
        return true;
    }

    /** Stops the module, as a bug would. */
    static void crash()
    {
        std::abort();
    }

    /** Asks for as much memory as the heap can ever have, which fails at once without using any. */
    static void exhaustMemory()
    {
        // Volatile, so the compiler can't leave out the request.
        char* volatile taken = new char[emscripten_get_heap_max()];
        delete[] taken;
    }
};

} // namespace

EMSCRIPTEN_BINDINGS(Testing)
{
    emscripten::class_<Testing>("Testing")
        .class_function("loseGpu", &Testing::loseGpu)
        .class_function("crash", &Testing::crash)
        .class_function("exhaustMemory", &Testing::exhaustMemory);
}

EMSCRIPTEN_BINDINGS(Viewer)
{
    emscripten::value_object<Gpu::Type::Info>("GpuInfo")
        .field("vendor", &Gpu::Type::Info::vendor)
        .field("device", &Gpu::Type::Info::device)
        .field("architecture", &Gpu::Type::Info::architecture)
        .field("description", &Gpu::Type::Info::description)
        .field("backend", &Gpu::Type::Info::backend)
        .field("adapter", &Gpu::Type::Info::adapter)
        .field("webglRenderer", &Gpu::Type::Info::webglRenderer);

    emscripten::value_object<WebTradeStats>("TradeStats")
        .field("count", &WebTradeStats::count)
        .field("firstMs", &WebTradeStats::firstMs)
        .field("lastMs", &WebTradeStats::lastMs)
        .field("totalAmount", &WebTradeStats::totalAmount);

    emscripten::value_object<WebTimeRange>("TimeRange")
        .field("startMs", &WebTimeRange::startMs)
        .field("endMs", &WebTimeRange::endMs);

    emscripten::class_<WebViewer>("Viewer")
        .constructor<>()
        .function("init", &WebViewer::init)
        .function("info", &WebViewer::getInfo)
        .function("resize", &WebViewer::resize)
        .function("update", &WebViewer::update)
        .function("needsRender", &WebViewer::needsRender)
        .function("render", &WebViewer::render)
        .function("view", &WebViewer::getView)
        .function("viewStats", &WebViewer::getViewStats)
        .function("axes", &WebViewer::getAxes)
        .function("legend", &WebViewer::getLegend)
        .function("setTheme", &WebViewer::setTheme)
        .function("setPointer", &WebViewer::setPointer)
        .function("clearPointer", &WebViewer::clearPointer)
        .function("pickedTrade", &WebViewer::getPickedTrade)
        .function("pickedColumn", &WebViewer::getPickedColumn)
        .function("summaries", &WebViewer::getSummaries)
        .function("setSummariesAllowed", &WebViewer::setSummariesAllowed)
        .function("setSummaryThreshold", &WebViewer::setSummaryThreshold)
        .function("setPlotInsets", &WebViewer::setPlotInsets)
        .function("resetView", &WebViewer::resetView)
        .function("showDay", &WebViewer::showDay)
        .function("zoomAt", &WebViewer::zoomAt)
        .function("panBy", &WebViewer::panBy)
        .function("setMarkerScale", &WebViewer::setMarkerScale)
        .function("setPixelRatio", &WebViewer::setPixelRatio)
        .function("tradeStats", &WebViewer::getTradeStats)
        .function("instrument", &WebViewer::getInstrument)
        .function("beginLoad", &WebViewer::beginLoad)
        .function("appendChunk", &WebViewer::appendChunk)
        .function("finishLoad", &WebViewer::finishLoad)
        .function("showScene", &WebViewer::showScene)
        .function("readPixels", &WebViewer::readPixels)
        .function("gpuHealth", &WebViewer::getGpuHealth)
        .function("memoryStats", &WebViewer::getMemoryStats)
        .function("setWakeCallback", &WebViewer::setWakeCallback)
        .function("setProfiling", &WebViewer::setProfiling)
        .function("setStatsCallback", &WebViewer::setStatsCallback);

    emscripten::function("scenes", &getScenes);
}
