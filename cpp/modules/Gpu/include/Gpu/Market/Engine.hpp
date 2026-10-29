#pragma once

#include "Gpu/Market/Type.hpp"
#include "Gpu/Type.hpp"

#include "Core/Data/Trades.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
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

    /**
     * Calls `onCreated` exactly once: natively before returning, on the web
     * later from the browser's event loop.
     */
    static void create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, CreatedCallback onDone);

    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] const Gpu::Type::Info& getInfo() const;

    /** Surface size in physical pixels. Zero sizes are ignored. */
    void resize(std::uint32_t width, std::uint32_t height);

    /** Physical pixels per CSS or window pixel, which marker sizes are given in. */
    void setPixelRatio(float ratio);

    /** Replaces the trades, which must be in time order. */
    void setTrades(const Core::Data::Trades& trades);

    /**
     * Adds trades after the current ones; they must not be earlier. If they
     * don't fit in the room reserved, the GPU copies everything to a bigger
     * buffer, which is slow.
     */
    void appendTrades(const Core::Data::Trades& trades);

    /** Makes room for `count` trades in total, for appending without copies. */
    void reserveTrades(std::size_t count);

    /** What part of the trades to show. */
    void setView(const Type::View& view);

    void setMarkerScale(Type::MarkerScale scale);

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

    /**
     * Copies the last frame of an offscreen target back to the CPU. Calls
     * `onDone` exactly once, usually from processEvents().
     */
    void readPixels(PixelsCallback onDone);
#endif

private:
    struct Impl;

    explicit Engine(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> mImpl;
};

} // namespace Gpu::Market
