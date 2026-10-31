#include "Viewer/App.hpp"

#include "Core/Plot/Axis.hpp"
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

    /** The trades on show. Empty until init() succeeds. */
    WebTradeStats getTradeStats() const
    {
        return *mApp ? toWeb(Core::Data::getStats((*mApp)->getTrades())) : WebTradeStats{};
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
        .function("beginLoad", &WebViewer::beginLoad)
        .function("appendChunk", &WebViewer::appendChunk)
        .function("finishLoad", &WebViewer::finishLoad);
}
