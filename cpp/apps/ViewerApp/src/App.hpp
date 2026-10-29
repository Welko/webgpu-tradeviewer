#pragma once

#include "Core/Data/TimeRange.hpp"
#include "Core/Data/Trades.hpp"
#include "Gpu/Market/Engine.hpp"
#include "Gpu/Market/Type.hpp"
#include "Gpu/Type.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace Viewer {

/** Synthetic trades for 2019-12-01 UTC, the brief's day, to show until real ones are loaded. */
[[nodiscard]] Core::Data::Trades syntheticDay();

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
    [[nodiscard]] const Core::Data::Trades& trades() const;

    /** Replaces the trades, which must be in time order, and resets the view. */
    void setTrades(Core::Data::Trades trades);

    /** The trades in view: how many, and their total amount. */
    struct Visible
    {
        std::size_t count;
        double amount;
    };

    /** The trades in the time range on screen, start inclusive and end exclusive. */
    [[nodiscard]] Visible visible() const;

    /** The time range on screen, left edge to right edge. */
    [[nodiscard]] Core::Data::TimeRange view() const { return mView; }

    /** 12:00 to 13:00 UTC on the day of the first trade. */
    void resetView();

    /** The whole UTC day of the first trade. */
    void showDay();

    /** Scales the time range by `factor` (above 1 zooms out), keeping the time at `x` pixels from the left in place. */
    void zoomAt(double x, double factor);

    /** Follows a drag of `dx` pixels: dragging right shows earlier trades. */
    void panBy(double dx);

    void setMarkerScale(Gpu::Market::Type::MarkerScale scale);

    /** Physical pixels per CSS or window pixel. Pointer positions are in physical pixels. */
    void setPixelRatio(float ratio);

    /** The GPU in use. */
    [[nodiscard]] const Gpu::Type::Info& info() const;

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
    /** Shows mView: finds its trades and fits the price range to them. */
    void applyView();

    std::unique_ptr<Gpu::Market::Engine> mEngine;
    Core::Data::Trades mTrades;
    Core::Data::AmountTotals mTotals;
    std::int64_t mDayStartUs = 0;  // midnight UTC before the first trade
    Core::Data::TimeRange mView{};
    Core::Data::PriceRange mPrice{};  // kept while the view has no trades
    Core::Data::IndexRange mVisible{};
    std::uint32_t mWidth = 0;
};

} // namespace Viewer
