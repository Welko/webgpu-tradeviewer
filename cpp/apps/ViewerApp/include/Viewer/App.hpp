#pragma once

#include "Core/Plot/Axis.hpp"
#include "Core/Range.hpp"
#include "Core/Data/Columns.hpp"
#include "Core/Plot/Pick.hpp"
#include "Core/Data/PriceTree.hpp"
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
#include <optional>
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
    [[nodiscard]] Core::Data::TradeStats getViewStats() const;

    /** The time range on screen, left edge to right edge. */
    [[nodiscard]] Core::TimeRange getView() const { return mView.time; }

    /** 12:00 to 13:00 UTC on the day of the first trade. */
    void resetView();

    /** The whole UTC day of the first trade. */
    void showDay();

    /** Ticks for the axes of the current view, also drawn as grid lines. */
    struct Axes
    {
        std::vector<Core::Plot::Axis::Tick> times;
        std::vector<Core::Plot::Axis::Tick> prices;
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

    /** The colors of the plot and the trades. */
    void setTheme(const Gpu::Market::Type::Theme& theme);

    /** Colors and reference sizes of the markers, at the current marker scale and theme. */
    [[nodiscard]] Gpu::Market::Type::Legend getLegend() const;

    /** Physical pixels per CSS or window pixel. Pointer positions are in physical pixels. */
    void setPixelRatio(float ratio);

    /** A trade the cursor points at, and where it's drawn. */
    struct PickedTrade
    {
        std::size_t index;
        std::int64_t timeUs;
        double price;
        double amount;
        Core::Data::Side side;
        /** Its marker's center, in physical pixels from the surface's top left corner. */
        double centerX;
        double centerY;
    };

    /**
     * Moves the cursor to (`x`, `y`), in physical pixels from
     * the surface's top left corner, and picks the trade it points at: the
     * nearest center within 24 CSS or window pixels, else a marker under the
     * pointer (see Core::Plot::pickTrade). While the view is summarized, it
     * picks the nearest column's bar instead (Core::Plot::pickColumn).
     * Zooming and panning pick again.
     */
    void setPointer(double x, double y);

    /** The pointer left the surface, so nothing is picked. */
    void clearPointer();

    [[nodiscard]] std::optional<PickedTrade> getPickedTrade() const;

    /** A column summary the cursor points at, while summaries are shown, and where it's drawn. */
    struct PickedColumn
    {
        Core::Data::ColumnSummary summary;
        Core::TimeRange time;  // the column's, start inclusive and end exclusive
        /** Its bar's center across, and its top and bottom, in physical pixels from the surface's top left corner. */
        double centerX;
        double top;
        double bottom;
    };

    [[nodiscard]] std::optional<PickedColumn> getPickedColumn() const;

    /**
     * Whether a crowded view may show column summaries instead of its trades
     * (the default), or always every trade, however slow.
     */
    void setSummariesAllowed(bool allowed);

    /**
     * How crowded a view must be for summaries: more trades than this per CSS
     * or window pixel column of the plot, on average. 32 by default.
     */
    void setSummaryThreshold(double tradesPerColumn);

    /**
     * The column summaries on show, while the view is summarized: those with
     * trades, of getColumnsAcross(). Empty while trades are shown.
     */
    [[nodiscard]] const std::vector<Core::Data::ColumnSummary>& getColumns() const { return mSummaries.columns; }

    /**
     * How many columns split the plot while the view is summarized, one per
     * CSS or window pixel, including those without trades; 0 while trades
     * are shown.
     */
    [[nodiscard]] std::uint32_t getColumnsAcross() const { return mSummaries.across; }

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

    /** Shows column summaries or trades, whichever the view's crowding calls for. */
    void updateSummaries();

    /** Picks the trade or column at the cursor again, after it or the view moved, and highlights it. */
    void updatePick();

    /** Where the view's trades are drawn, in physical pixels within the plot. */
    [[nodiscard]] Core::Plot::Mapping getPlotMapping() const;

    /** The plot's size in physical pixels: the surface less the insets. */
    [[nodiscard]] double getPlotWidth() const;
    [[nodiscard]] double getPlotHeight() const;

    /** The trades, and what's worked out from them when they're set. */
    struct TradeData
    {
        Core::Data::Trades trades;
        Core::Data::RunningTotals totals;
        Core::Data::PriceTree prices;
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
        std::uint32_t surfaceWidth = 0;  // physical pixels
        std::uint32_t surfaceHeight = 0;
        float pixelRatio = 1.0f;
        Gpu::Plot::Type::Insets insets{};
    };

    /** Where the pointer is, and the trade it picks. */
    struct Cursor
    {
        bool onSurface = false;
        double x = 0.0;  // physical pixels from the surface's top left corner
        double y = 0.0;
        std::optional<std::size_t> trade;
        std::optional<std::size_t> column;  // by position in mSummaries.columns
    };

    /** Whether the view shows column summaries, when it's crowded, instead of its trades. */
    struct Summaries
    {
        bool allowed = true;
        double threshold = 32.0;  // trades per CSS or window pixel column, on average
        std::uint32_t across = 0;  // columns splitting the plot while shown, 0 while trades are
        std::vector<Core::Data::ColumnSummary> columns;  // those with trades, so up to `across`
    };

    std::unique_ptr<Gpu::Market::Engine> mEngine;
    TradeData mData;
    View mView;
    Layout mLayout;
    Cursor mCursor;
    Summaries mSummaries;
    Gpu::Market::Type::MarkerScale mMarkerScale = Gpu::Market::Type::MarkerScale::Area;
    Gpu::Market::Type::Theme mTheme;
};

} // namespace Viewer
