#pragma once

#include "Core/Axis.hpp"
#include "Core/Range.hpp"
#include "Core/Data/Trades.hpp"
#include "Gpu/Market/Engine.hpp"
#include "Gpu/Market/Type.hpp"
#include "Gpu/Plot/Type.hpp"
#include "Gpu/Type.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Viewer {

/** Synthetic trades for 2019-12-01 UTC, the brief's day, to show until real ones are loaded. */
[[nodiscard]] Core::Data::Trades generateSyntheticDay();

/**
 * The platform-independent app. Each platform's main owns the loop: update(),
 * then render() only if needsRender(), otherwise sleep.
 */
class App
{
public:
    using Result = std::expected<std::unique_ptr<App>, std::string>;
    using Callback = std::function<void(Result)>;

    /** Calls `onDone` exactly once, possibly before returning. */
    static void create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone);

    explicit App(std::unique_ptr<Gpu::Market::Engine> engine) noexcept;

    /** The trades on show: none until setTrades(). */
    [[nodiscard]] const Core::Data::Trades& getTrades() const;

    /** Replaces the trades, which must be in time order, and resets the view. */
    void setTrades(Core::Data::Trades trades);

    /** The trades in the time range on screen, start inclusive and end exclusive. */
    [[nodiscard]] Core::Data::TradeSummary getViewSummary() const;

    /** The time range on screen, left edge to right edge. */
    [[nodiscard]] Core::TimeRange getView() const { return mView.time; }

    /** 12:00 to 13:00 UTC on the day of the first trade. */
    void resetView();

    /** The whole UTC day of the first trade. */
    void showDay();

    /** Ticks for the axes of the current view, also drawn as grid lines. */
    struct Axes
    {
        std::vector<Core::Axis::Tick> times;
        std::vector<Core::Axis::Tick> prices;
    };

    [[nodiscard]] const Axes& getAxes() const { return mView.axes; }

    /** Space to leave around the plot for axis labels, in physical pixels. */
    void setPlotInsets(Gpu::Plot::Type::Insets insets);

    /**
     * Scales the time range by `factor` (above 1 zooms out), keeping the time
     * at `x` pixels from the surface's left edge in place.
     */
    void zoomAt(double x, double factor);

    /** Follows a drag of `dx` pixels: dragging right shows earlier trades. */
    void panBy(double dx);

    void setMarkerScale(Gpu::Market::Type::MarkerScale scale);

    /** Physical pixels per CSS or window pixel. Pointer positions are in physical pixels. */
    void setPixelRatio(float ratio);

    /** The GPU in use. */
    [[nodiscard]] const Gpu::Type::Info& getInfo() const;

    /** Surface size in physical pixels. */
    void resize(std::uint32_t width, std::uint32_t height);

    /** Advances the app's state to `nowMs`. */
    void update(double nowMs);

    /** Whether the current state differs from the last drawn frame. */
    [[nodiscard]] bool needsRender() const;

    /** Draws the current state. */
    void render();

    /** Turns frame timing on or off. Off by default. */
    void setProfiling(bool enabled);

    /** Calls `onStats` once per drawn frame, in order, once its GPU times are in. */
    void setStatsCallback(Gpu::Market::Engine::StatsCallback onStats);

#ifndef __EMSCRIPTEN__
    /** Delivers finished asynchronous GPU work. Call every loop iteration. */
    void processEvents();
#endif

private:
    /** Shows mView.time: finds its trades and fits the price range to them. */
    void applyView();

    /** Ticks and grid lines for the current view and plot size. */
    void updateAxes();

    /** The plot's size in physical pixels: the surface less the insets. */
    [[nodiscard]] double getPlotWidth() const;
    [[nodiscard]] double getPlotHeight() const;

    /** The trades, and what's worked out from them when they're set. */
    struct TradeData
    {
        Core::Data::Trades trades;
        Core::Data::AmountTotals totals;
        std::int64_t dayStartUs = 0;  // midnight UTC before the first trade
    };

    /** What's on screen; applyView() updates it all. */
    struct View
    {
        Core::TimeRange time{};
        Core::ValueRange price{};  // kept while the view has no trades
        Core::IndexRange trades{};  // the trades in `time`
        Axes axes;
    };

    /** The surface, and the plot inside it. */
    struct Layout
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float pixelRatio = 1.0f;
        Gpu::Plot::Type::Insets insets{};
    };

    std::unique_ptr<Gpu::Market::Engine> mEngine;
    TradeData mData;
    View mView;
    Layout mLayout;
};

} // namespace Viewer
