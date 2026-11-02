#pragma once

#include "Gpu/Market/Type.hpp"
#include "Gpu/Plot/Type.hpp"
#include "Gpu/Type.hpp"

#include "Core/Data/Trades.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Gpu::Market {

/**
 * Draws market data with WebGPU.
 */
class Engine
{
public:
    using CreatedResult = std::expected<std::unique_ptr<Engine>, std::string>;
    using CreatedCallback = std::function<void(CreatedResult)>;

    using Pixels = std::vector<std::uint8_t>;
    using PixelsCallback = std::function<void(std::expected<Pixels, std::string>)>;

    using StatsCallback = std::function<void(const Gpu::Type::FrameStats&)>;

    /** What an engine draws to, on which GPU, and how much it may hold. */
    struct Params
    {
        Gpu::Type::Target::Any target;
        Gpu::Type::PowerPreference powerPreference = Gpu::Type::PowerPreference::Default;
        /** At most this many trades, short of what the GPU holds; none for as many as it holds. */
        std::optional<std::size_t> maxTrades = std::nullopt;
    };

    /**
     * The settings of what a frame shows, set all at once: whoever drives
     * the engine keeps one, and a new engine gets the same picture from it in
     * one call. Only small values, cheap to compare and copy: the trades and
     * columns, which can be large, have their own calls.
     */
    struct Settings
    {
        /** Surface size in physical pixels. Zero sizes are ignored: offscreen, the target's own stays. */
        std::uint32_t widthPx = 0;
        std::uint32_t heightPx = 0;
        /** Physical pixels per CSS or window pixel, which marker sizes are given in. */
        float pixelRatio = 1.0f;
        /** Space to leave around the plot for axis labels. */
        Plot::Type::Insets insets{};
        /** Where to draw grid lines, usually at the axis ticks. */
        Plot::Type::Grid grid{};
        /** What part of the trades to show. */
        Type::View view{};
        Type::MarkerScale markerScale = Type::MarkerScale::Area;
        /** The colors of everything drawn. */
        Type::Theme theme{};
        /** The column, by position in those set with setColumns(), to draw in the cursor color. */
        std::optional<std::size_t> highlightedColumn{};
        /** The trade, by index among those set, to ring, such as the one under the cursor. */
        std::optional<std::size_t> highlightedTrade{};

        bool operator==(const Settings&) const = default;
    };

    /**
     * Calls `onCreated` exactly once: natively before returning, on the web
     * later from the browser's event loop.
     */
    static void create(const Params& params, CreatedCallback onDone);

    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] const Gpu::Type::Info& getInfo() const;

    /** Shows `settings` from the next frame on. Only the parts that changed cost anything. Settings' defaults until called. */
    void setSettings(const Settings& settings);

    /**
     * Draws these summaries instead of the view's trades, until called with
     * nothing. For views too crowded to show every trade.
     */
    void setColumns(const std::optional<Type::Columns>& columns);

    /** The most trades the engine holds: as many as fit in the GPU's largest buffer, or Params::maxTrades. */
    [[nodiscard]] std::size_t getMaxTrades() const;

    /** Replaces the trades, which must be in time order. Fails, keeping the old ones, past getMaxTrades(). */
    [[nodiscard]] std::expected<void, std::string> setTrades(const Core::Data::Trades& trades);

    /**
     * Adds trades after the current ones; they must not be earlier. If they
     * don't fit in the room reserved, the GPU copies everything to a bigger
     * buffer, which is slow. Fails, adding none, past getMaxTrades().
     */
    [[nodiscard]] std::expected<void, std::string> appendTrades(const Core::Data::Trades& trades);

    /** Makes room for `count` trades in total, up to getMaxTrades(), for appending without copies. */
    void reserveTrades(std::size_t count);

    /** Advances the engine's state to `nowMs`. */
    void update(double nowMs);

    /**
     * Whether something changed since the last frame, or an animation is
     * running. When false, the app can sleep until the next input.
     */
    [[nodiscard]] bool needsRender() const;

    /** Draws a frame, needed or not. */
    void render();

    /** Turns frame timing on or off. Off by default. */
    void setProfiling(bool enabled);

    /**
     * Calls `onStats` once for each frame, in order, once its GPU times are
     * in (usually a few frames later). Mustn't call render().
     */
    void setStatsCallback(const StatsCallback& onStats);

#ifndef __EMSCRIPTEN__
    /** Delivers finished asynchronous work, such as readPixels() results and GPU times. */
    void processEvents();
#endif

    /**
     * Copies a frame back to the CPU as RGBA rows, top row first: offscreen,
     * the last one drawn; on a window or canvas, the next one, since a shown
     * frame is gone. Calls `onDone` once, unless the engine is destroyed
     * first: natively from processEvents(), on the web from the browser's
     * event loop.
     */
    void readPixels(PixelsCallback onDone);

    /**
     * Whether the GPU device is gone, after a driver reset or a crash of the
     * browser's GPU process, for example. Nothing draws any more, and every
     * call does nothing: the app must create a new engine and set it up
     * again.
     */
    [[nodiscard]] bool isLost() const;

    /**
     * Called once when the device is lost: natively from processEvents(), on
     * the web from the browser's event loop. It mustn't destroy the engine.
     */
    void setLostCallback(std::function<void()> onLost);

    /** Loses the device, as a GPU reset would: WebGPU's destroy(). For tests of what follows. */
    void loseDevice();

    /**
     * The first WebGPU errors nothing caught (up to 16), such as validation
     * errors, which mean a bug, or running out of GPU memory.
     */
    [[nodiscard]] const std::vector<std::string>& getErrors() const;

    /** How many such errors there were, including those not kept. */
    [[nodiscard]] std::size_t getErrorCount() const;

    /** The GPU memory the engine's buffers and textures take, a window's or canvas's own excepted. */
    [[nodiscard]] std::uint64_t getGpuBytes() const;

private:
    struct Impl;

    explicit Engine(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> mImpl;
};

} // namespace Gpu::Market
