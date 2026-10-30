#include "Viewer/App.hpp"

#include "Core/Axis.hpp"
#include "Core/Data/TradeParser.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using emscripten::val;

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

/** Core::Data::TradeSummary in numbers JavaScript reads exactly, with times in milliseconds for Date. */
struct WebTradeSummary
{
    double count;
    double firstMs;
    double lastMs;
    double totalAmount;
};

/** Ticks as a JavaScript array of { fraction, label }. */
val toWeb(const std::vector<Core::Axis::Tick>& ticks)
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

/** A colour as a JavaScript { red, green, blue }, each from 0 to 1. */
val toWeb(Gpu::Type::Color color)
{
    val object = val::object();
    object.set("red", color.red);
    object.set("green", color.green);
    object.set("blue", color.blue);
    return object;
}

/** A legend as { buy, sell, opacity, sizes: [{ amount, radius, bound }] }, `bound` being "exact", "atMost" or "atLeast". */
val toWeb(const Gpu::Market::Type::Legend& legend)
{
    using Bound = Gpu::Market::Type::LegendSize::Bound;
    val sizes = val::array();
    for (const auto& size : legend.sizes) {
        val object = val::object();
        object.set("amount", size.amount);
        object.set("radius", size.radius);
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

WebTradeSummary toWeb(const Core::Data::TradeSummary& summary)
{
    return {
        .count = static_cast<double>(summary.count),
        .firstMs = static_cast<double>(summary.firstUs) / 1000.0,
        .lastMs = static_cast<double>(summary.lastUs) / 1000.0,
        .totalAmount = summary.totalAmount,
    };
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
        Viewer::App::create(Gpu::Type::Target::Canvas{ .selector = std::move(selector) }, parsePowerPreference(powerPreference),
            [slot, onDone](Viewer::App::Result result) {
                auto app = slot.lock();
                if (!app) {
                    // delete() was called before initialization finished.
                    return;
                }
                if (!result) {
                    onDone(result.error());
                    return;
                }
                *app = std::move(*result);
                (*app)->setTrades(Viewer::generateSyntheticDay());
                onDone(val::null());
            });
    }

    /** The GPU in use, as a plain JavaScript object. Empty until init() succeeds. */
    Gpu::Type::Info getInfo() const
    {
        return *mApp ? (*mApp)->getInfo() : Gpu::Type::Info{};
    }

    void resize(std::uint32_t width, std::uint32_t height)
    {
        if (*mApp) {
            (*mApp)->resize(width, height);
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
    WebTradeSummary getViewSummary() const
    {
        return *mApp ? toWeb((*mApp)->getViewSummary()) : WebTradeSummary{};
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

    /** Takes { background, grid, buy, sell, markerOpacity }, each colour a { red, green, blue } from 0 to 1. */
    void setTheme(const val& theme)
    {
        if (*mApp) {
            (*mApp)->setTheme({
                .plot = { .background = toColor(theme["background"]), .grid = toColor(theme["grid"]) },
                .buy = toColor(theme["buy"]),
                .sell = toColor(theme["sell"]),
                .markerOpacity = theme["markerOpacity"].as<float>(),
            });
        }
    }

    /** What markers look like at the current marker scale, for a legend; null before init() succeeds. */
    val getLegend() const
    {
        return *mApp ? toWeb((*mApp)->getLegend()) : val::null();
    }

    /** Space around the plot for axis labels, in physical pixels. */
    void setPlotInsets(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom)
    {
        if (*mApp) {
            (*mApp)->setPlotInsets({ .left = left, .top = top, .right = right, .bottom = bottom });
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

    /** `x` in physical pixels from the canvas's left edge; a `factor` above 1 zooms out. */
    void zoomAt(double x, double factor)
    {
        if (*mApp) {
            (*mApp)->zoomAt(x, factor);
        }
    }

    /** `dx` in physical pixels. */
    void panBy(double dx)
    {
        if (*mApp) {
            (*mApp)->panBy(dx);
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

    /** The trades on show. Empty until init() succeeds. */
    WebTradeSummary getTradeSummary() const
    {
        return *mApp ? toWeb(Core::Data::summarize((*mApp)->getTrades())) : WebTradeSummary{};
    }

    /** Starts reading a trades file, which appendChunk() then receives piece by piece. */
    void beginLoad()
    {
        mParser.emplace();
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
        (*mApp)->setTrades(std::move(*trades));
        return {};
    }

private:
    std::shared_ptr<std::unique_ptr<Viewer::App>> mApp = std::make_shared<std::unique_ptr<Viewer::App>>();
    std::optional<Core::Data::TradeParser> mParser;  // while a file loads
    std::vector<char> mChunk;  // the last chunk, copied out of JavaScript
};

} // namespace

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

    emscripten::value_object<WebTradeSummary>("TradeSummary")
        .field("count", &WebTradeSummary::count)
        .field("firstMs", &WebTradeSummary::firstMs)
        .field("lastMs", &WebTradeSummary::lastMs)
        .field("totalAmount", &WebTradeSummary::totalAmount);

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
        .function("viewSummary", &WebViewer::getViewSummary)
        .function("axes", &WebViewer::getAxes)
        .function("legend", &WebViewer::getLegend)
        .function("setTheme", &WebViewer::setTheme)
        .function("setPlotInsets", &WebViewer::setPlotInsets)
        .function("resetView", &WebViewer::resetView)
        .function("showDay", &WebViewer::showDay)
        .function("zoomAt", &WebViewer::zoomAt)
        .function("panBy", &WebViewer::panBy)
        .function("setMarkerScale", &WebViewer::setMarkerScale)
        .function("setPixelRatio", &WebViewer::setPixelRatio)
        .function("tradeSummary", &WebViewer::getTradeSummary)
        .function("beginLoad", &WebViewer::beginLoad)
        .function("appendChunk", &WebViewer::appendChunk)
        .function("finishLoad", &WebViewer::finishLoad);
}
