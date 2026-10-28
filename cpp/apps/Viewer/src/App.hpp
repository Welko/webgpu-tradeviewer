#pragma once

#include "Core/Data/Trades.hpp"
#include "Gpu/Market/Engine.hpp"
#include "Gpu/Type.hpp"

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

    void setTrades(Core::Data::Trades trades);

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
    std::unique_ptr<Gpu::Market::Engine> mEngine;
    Core::Data::Trades mTrades;
};

} // namespace Viewer
