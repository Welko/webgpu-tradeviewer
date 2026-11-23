#pragma once

#include "Web/Convert.hpp"

#include "Viewer/App.hpp"

#include "Core/Data/TradeParser.hpp"
#include "Gpu/Type.hpp"

#include <emscripten/val.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Viewer::Web {

struct Testing;

/**
 * The object JavaScript holds, as `Viewer`. JavaScript owns the frame loop
 * and must call delete() when done. Until init() succeeds, calls do nothing,
 * and getters say nothing: null, or an empty value. Positions are in physical
 * pixels from the canvas's top-left corner.
 */
class Bridge
{
public:
    /** Calls `onDone(error)` once: null on success, a message on failure. */
    void init(std::string selector, const std::string& powerPreference, val onDone);

    [[nodiscard]] Gpu::Type::Info getInfo() const;

    // Frames

    void resize(std::uint32_t widthPx, std::uint32_t heightPx);
    void update(double nowMs);
    [[nodiscard]] bool needsRender() const;
    void render();
    /** Calls `onWake()` when the viewer needs a frame without any input: when the GPU is lost, and once it's back. */
    void setWakeCallback(val onWake);

    // What's shown

    [[nodiscard]] TimeRange getView() const;
    [[nodiscard]] TradeStats getViewStats() const;
    /** { times, prices }, each an array of { fraction, label }. */
    [[nodiscard]] val getAxes() const;
    /** What markers look like at the current marker scale, for a legend (see Convert::ToWeb::legend()). */
    [[nodiscard]] val getLegend() const;
    /** While the view shows column summaries, { across, withTrades }: the columns, and those with trades. Otherwise null. */
    [[nodiscard]] val getSummaries() const;
    [[nodiscard]] TradeStats getTradeStats() const;
    [[nodiscard]] val getInstrument() const;

    // Changing it

    void setTheme(const val& theme);
    void setPlotInsets(std::uint32_t leftPx, std::uint32_t topPx, std::uint32_t rightPx, std::uint32_t bottomPx);
    void setPixelRatio(float ratio);
    void setMarkerScale(const std::string& scale);
    void setSummariesAllowed(bool allowed);
    void setSummaryThreshold(double tradesPerColumn);
    /** Back to 12:00–13:00 on the day of the first trade. */
    void resetView();
    /** The whole day of the first trade. */
    void showDay();
    /** A `factor` above 1 zooms out. */
    void zoomAt(double xPx, double factor);
    void panBy(double dxPx);

    // The pointer

    void setPointer(double xPx, double yPx);
    void clearPointer();
    /** The trade under the pointer (see Convert::ToWeb::pickedTrade()), or null. */
    [[nodiscard]] val getPickedTrade() const;
    /** The column summary under the pointer (see Convert::ToWeb::pickedColumn()), or null. */
    [[nodiscard]] val getPickedColumn() const;

    // Loading a file

    /**
     * Starts reading a trades file, which appendChunk() then receives piece
     * by piece. Its name gives the assets when the file doesn't, as
     * Binance's don't: BTCUSDT-trades-2019-12-01.zip.
     */
    void beginLoad(const std::string& fileName);
    /** Parses a Uint8Array of the file. { error, warnings }, as finishLoad() gives them: warnings come only at the end. */
    [[nodiscard]] val appendChunk(const val& bytes);
    /**
     * Shows the file's trades in place of the current ones. { error, warnings }:
     * why it failed, or null, keeping the trades on show; and what about the
     * trades makes what's shown differ from the file, or look wrong.
     */
    [[nodiscard]] val finishLoad();

    // Diagnostics

    [[nodiscard]] val getGpuHealth() const;
    [[nodiscard]] val getMemoryStats() const;
    void setProfiling(bool enabled);
    /** Calls `onStats(stats)` once per drawn frame, in order, once its GPU times are in, or stops with null. It mustn't call render(). */
    void setStatsCallback(val onStats);

    // Telemetry

    /**
     * Starts sending anonymous telemetry, once the user agreed: each event as
     * `onEvent(name, properties)`, the first with `environment` (browser,
     * screen...) added. Null stops at once.
     */
    void setTelemetry(val onEvent, const val& environment);
    void flushTelemetry();
    /** What every event carries, for events the page sends itself, such as errors. */
    [[nodiscard]] val getTelemetryProperties() const;

    // Image tests

    /** Shows the scene called `name` (see Viewer::Scene) in place of everything else. Its known pixels, or null if there's none. */
    [[nodiscard]] val showScene(const std::string& name);
    /**
     * Copies the next frame drawn back, then calls `onDone(pixels, error)`:
     * a Uint8Array of RGBA rows, top row first, and null; or null and a message.
     */
    void readPixels(val onDone);

private:
    friend struct Testing;

    /** `use(app)` once init() has succeeded; nothing before. */
    template<typename Use>
    void withApp(Use use) const
    {
        if (*mApp)
        {
            use(**mApp);
        }
    }

    /** `use(app)` once init() has succeeded; `otherwise` before. */
    template<typename Use, typename Result>
    Result withApp(Use use, Result otherwise) const
    {
        return *mApp ? Result(use(**mApp)) : otherwise;
    }

    // Shared, so a creation that finishes after delete() finds it gone.
    std::shared_ptr<std::unique_ptr<App>> mApp = std::make_shared<std::unique_ptr<App>>();
    std::optional<Core::Data::TradeParser> mParser;  // while a file loads
    std::vector<char> mChunk;  // the last chunk, copied out of JavaScript
};

}  // namespace Viewer::Web
