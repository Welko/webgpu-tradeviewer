#pragma once

#include "Core/Plot/Axis.hpp"
#include "Core/Range.hpp"
#include "Core/Data/Columns.hpp"
#include "Core/Plot/Pick.hpp"
#include "Core/Data/PreprocessedTrades.hpp"
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
#include <variant>
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
    static void create(const Gpu::Market::Engine::Params& params, Callback onDone);

    /** `engine` was created with `params`, which make a new one if the GPU is lost. */
    App(std::unique_ptr<Gpu::Market::Engine> engine, Gpu::Market::Engine::Params params) noexcept;

    /** The trades on show: none until setTrades(). */
    [[nodiscard]] const Core::Data::Trades& getTrades() const;

    /**
     * Replaces the trades, which must be in time order, and resets the view.
     * Takes them, since they're large: pass them with std::move. Fails if the
     * GPU can't hold them all, keeping the old ones and leaving `trades` as
     * they were.
     */
    [[nodiscard]] std::expected<void, std::string> setTrades(Core::Data::Trades&& trades);

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
     * `xPx` from the surface's left edge in place.
     */
    void zoomAt(double xPx, double factor);

    /** Follows a drag of `dxPx`: dragging right shows earlier trades. */
    void panBy(double dxPx);

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
        double centerXPx;
        double centerYPx;
    };

    /**
     * Moves the cursor to (`xPx`, `yPx`), from the surface's top
     * left corner, and picks the trade it points at: the nearest center within
     * 24 CSS or window pixels, else a marker under the pointer (see
     * Core::Plot::pickTrade). While the view is summarized, it picks the
     * nearest column's bar instead (Core::Plot::pickColumn). Zooming and
     * panning pick again.
     */
    void setPointer(double xPx, double yPx);

    /** The pointer left the surface, so nothing is picked. */
    void clearPointer();

    [[nodiscard]] std::optional<PickedTrade> getPickedTrade() const;

    /** A column summary the cursor points at, while summaries are shown, and where it's drawn. */
    struct PickedColumn
    {
        Core::Data::ColumnSummary summary;
        Core::TimeRange time;  // the column's, start inclusive and end exclusive
        /** Its bar's center across, and its top and bottom, in physical pixels from the surface's top left corner. */
        double centerXPx;
        double topPx;
        double bottomPx;
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
    void resize(std::uint32_t widthPx, std::uint32_t heightPx);

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

    /**
     * Copies the next frame drawn back as RGBA rows, top row first (see
     * Gpu::Market::Engine::readPixels). Fails at once while the GPU restarts.
     */
    void readPixels(Gpu::Market::Engine::PixelsCallback onDone);

    /**
     * How the GPU is doing. When it's lost (a driver reset, the browser's GPU
     * process crashing...), the next update() creates a new engine on a new
     * device, and sets it up as the old one was: trades, view, size, colors.
     */
    struct GpuHealth
    {
        bool restarting = false;  // lost, and a new device is on its way
        std::size_t restarts = 0;  // how many times it was lost and came back
        std::optional<std::string> failure;  // why it couldn't come back; nothing draws
        std::vector<std::string> errors;  // the first WebGPU errors nothing caught (see Engine::getErrors)
        std::size_t errorCount = 0;
    };

    [[nodiscard]] GpuHealth getGpuHealth() const;

    /**
     * Called when the app needs update() and render() without any input:
     * when the GPU is lost, and once it's back. Possibly from processEvents()
     * natively, or the browser's event loop.
     */
    void setWakeCallback(std::function<void()> onWake);

    /** The memory the app's large parts hold, room to grow included. */
    struct MemoryStats
    {
        std::size_t tradesBytes = 0;  // 25 per trade
        std::size_t totalsBytes = 0;  // running totals, 20 per trade
        std::size_t priceTreeBytes = 0;  // about 2 per trade
        std::size_t summariesBytes = 0;  // the column summaries on show
        std::uint64_t gpuBytes = 0;  // buffers and textures: 20 per trade, and the columns'

        [[nodiscard]] std::size_t getCpuBytes() const { return tradesBytes + totalsBytes + priceTreeBytes + summariesBytes; }
    };

    [[nodiscard]] MemoryStats getMemoryStats() const;

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

    /** Draws the columns in mSummaries, or the trades when there are none. */
    void showColumns();

    /** Creates a new engine after the GPU was lost. */
    void restartGpu();

    /** Gives a new engine everything the old one had. */
    void setUpEngine();

    /** Midnight UTC before the first trade, or the Unix epoch with none. */
    [[nodiscard]] std::int64_t getDayStartUs() const;

    /** Where the view's trades are drawn, in physical pixels within the plot. */
    [[nodiscard]] Core::Plot::Mapping getPlotMapping() const;

    /** The plot's size in physical pixels: the surface less the insets. */
    [[nodiscard]] double getPlotWidthPx() const;
    [[nodiscard]] double getPlotHeightPx() const;

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
        std::uint32_t surfaceWidthPx = 0;  // physical pixels
        std::uint32_t surfaceHeightPx = 0;
        float pixelRatio = 1.0f;
        Gpu::Plot::Type::Insets insets{};
    };

    /** Where the pointer is, and the trade it picks. */
    struct Cursor
    {
        bool onSurface = false;
        double xPx = 0.0;  // physical pixels from the surface's top left corner
        double yPx = 0.0;
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

    /** The GPU works. */
    struct GpuWorking
    {};

    /** The GPU was lost, and a new engine is on its way. */
    struct GpuRestarting
    {};

    /** The GPU was lost and couldn't come back: nothing draws. */
    struct GpuFailed
    {
        std::string reason;
    };

    /** How the engine is made, how the GPU is doing, and what each new engine is given besides the app's state. */
    struct EngineSetup
    {
        Gpu::Market::Engine::Params params;
        Gpu::Type::Info info;  // of the engine in use, or the last one
        std::variant<GpuWorking, GpuRestarting, GpuFailed> gpu = GpuWorking{};
        std::size_t restarts = 0;  // times the GPU was lost and came back
        bool profiling = false;
        Gpu::Market::Engine::StatsCallback onStats{};
        std::function<void()> onWake{};
        // Expires with the app, for a new engine that arrives later.
        std::shared_ptr<int> alive = std::make_shared<int>();
    };

    // Null only while a new one is being created, after the GPU was lost.
    std::unique_ptr<Gpu::Market::Engine> mEngine;
    EngineSetup mSetup;
    Core::Data::PreprocessedTrades mData;
    View mView;
    Layout mLayout;
    Cursor mCursor;
    Summaries mSummaries;
    Gpu::Market::Type::MarkerScale mMarkerScale = Gpu::Market::Type::MarkerScale::Area;
    Gpu::Market::Type::Theme mTheme;
};

} // namespace Viewer
