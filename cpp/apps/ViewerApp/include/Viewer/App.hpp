#pragma once

#include "Core/Data/PreprocessedTrades.hpp"
#include "Core/Data/Trades.hpp"
#include "Core/Range.hpp"

#include "Gpu/Market/Engine.hpp"
#include "Gpu/Market/Style.hpp"
#include "Gpu/Market/Type.hpp"
#include "Gpu/Plot/Type.hpp"
#include "Gpu/Type.hpp"

#include "Viewer/Axes.hpp"
#include "Viewer/GpuSession.hpp"
#include "Viewer/Layout.hpp"
#include "Viewer/Pointer.hpp"
#include "Viewer/Summaries.hpp"
#include "Viewer/View.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace Viewer {

/** Synthetic trades for 2019-12-01 UTC, the brief's day, to show until real ones are loaded. */
[[nodiscard]] Core::Data::Trades generateSyntheticDay();

/**
 * The platform-independent app. Each platform's main owns the loop: update(),
 * then render() only if needsRender(), otherwise sleep.
 *
 * Positions, given and returned, are in physical pixels from the surface's
 * top-left corner, y going down.
 *
 * Its parts (the view, axes, summaries and pointer) can be read, but only the app
 * changes them, since each change can call for others and for the engine.
 */
class App
{
public:
    using Result = std::expected<std::unique_ptr<App>, std::string>;
    using Callback = std::function<void(Result)>;

    struct MemoryStats
    {
        std::size_t tradesBytes = 0;
        std::size_t totalsBytes = 0;  // running totals
        std::size_t priceTreeBytes = 0;
        std::size_t summariesBytes = 0;  // the column summaries on show
        std::uint64_t gpuBytes = 0;  // buffers and textures

        [[nodiscard]] std::size_t getCpuBytes() const { return tradesBytes + totalsBytes + priceTreeBytes + summariesBytes; }
    };

    /**
     * @param onDone Called exactly once, possibly before returning
     */
    static void create(const Gpu::Market::Engine::Params& params, Callback onDone);

    // Data
    [[nodiscard]] std::expected<void, std::string> setTrades(Core::Data::Trades&& trades);
    [[nodiscard]] const Core::Data::Trades& getTrades() const { return mData.getTrades(); }
    [[nodiscard]] const Core::Data::Instrument& getInstrument() const { return getTrades().instrument; }

    // Layout
    void setSize(std::uint32_t widthPx, std::uint32_t heightPx);
    void setPixelRatio(float ratio);
    void setPlotInsets(Gpu::Plot::Type::Insets insets);

    // View
    [[nodiscard]] const View& getView() const { return mView; }
    void setView(Core::TimeRange time);
    [[nodiscard]] const Axes& getAxes() const { return mAxes; }
    [[nodiscard]] std::optional<Core::TimeRange> getInitialView() const;
    void zoomAt(double xPx, double factor);
    void panBy(double dxPx);

    // Summaries
    [[nodiscard]] const Summaries& getSummaries() const { return mSummaries; }
    void setSummariesAllowed(bool allowed);
    void setSummaryThreshold(double tradesPerColumn);

    // Style
    void setTheme(const Gpu::Market::Type::Theme& theme);
    void setMarkerScale(Gpu::Market::Type::MarkerScale scale);
    [[nodiscard]] Gpu::Market::Type::Legend getLegend() const;
    [[nodiscard]] const Gpu::Market::Style::MarkerSizing& getMarkerSizing() const { return mMarkerSizing; }

    // Pointer
    [[nodiscard]] const Pointer& getPointer() const { return mPointer; }
    void setPointer(double xPx, double yPx);
    void clearPointer();

    // Loop
    void update(double nowMs);
    [[nodiscard]] bool needsRender() const;
    void render();
#ifndef __EMSCRIPTEN__
    void processEvents();
#endif
    void setWakeCallback(std::function<void()> onWake);
    void readPixels(Gpu::Market::Engine::PixelsCallback onDone);

    // Diagnostics
    [[nodiscard]] const Gpu::Type::Info& getInfo() const { return mGpu.getInfo(); }
    [[nodiscard]] GpuSession::Health getGpuHealth() const;
    [[nodiscard]] MemoryStats getMemoryStats() const;
    void setProfiling(bool enabled);
    void setStatsCallback(Gpu::Market::Engine::StatsCallback onStats);
    /** Loses the GPU device, as a driver reset would, for tests of what follows. False while there's none to lose. */
    [[nodiscard]] bool loseGpu();

private:
    App(std::unique_ptr<Gpu::Market::Engine> engine, Gpu::Market::Engine::Params params) noexcept;

    // Parts
    void applyView();
    void updateAxes();
    void updateSummaries();
    void updatePick();

    // Engine
    [[nodiscard]] Gpu::Market::Engine::Settings getEngineSettings() const;
    void syncSettings();
    [[nodiscard]] std::expected<void, std::string> setUpEngine(Gpu::Market::Engine& engine);

    Core::Data::PreprocessedTrades mData;
    Layout mLayout;
    View mView;
    Axes mAxes;
    Summaries mSummaries;
    Gpu::Market::Type::Theme mTheme{};
    Gpu::Market::Style::MarkerSizing mMarkerSizing{};
    Pointer mPointer;
    GpuSession mGpu;
};

} // namespace Viewer
