#pragma once

#include "Gpu/Market/Engine.hpp"
#include "Gpu/Type.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Viewer {

/**
 * An engine that outlives its GPU. When the GPU is lost (a driver reset, the
 * browser's GPU process crashing...), restartIfLost() creates a new engine on
 * a new device, and the set-up callback gives it what the old one showed.
 */
class GpuSession
{
public:
    /** Gives an engine what it should show. Fails if the engine can't hold it. */
    using SetUpCallback = std::function<std::expected<void, std::string>(Gpu::Market::Engine&)>;

    /** `engine` was created with `params`, which make each new one. Sets `engine` up with `onSetUp` at once. */
    GpuSession(std::unique_ptr<Gpu::Market::Engine> engine, Gpu::Market::Engine::Params params, SetUpCallback onSetUp) noexcept;

    GpuSession(const GpuSession&) = delete;
    GpuSession& operator=(const GpuSession&) = delete;

    /** The engine in use. Null while a new one is on its way, or if none could be made. */
    [[nodiscard]] Gpu::Market::Engine* getEngine() const { return mEngine.get(); }

    /** If the GPU was lost, creates a new engine. Call outside the engine's own calls, such as every update(). */
    void restartIfLost();

    /** The GPU in use, or the last one. */
    [[nodiscard]] const Gpu::Type::Info& getInfo() const { return mInfo; }

    /** How the GPU is doing. */
    struct Health
    {
        bool restarting = false;  // lost, and a new device is on its way
        std::size_t restarts = 0;  // how many times it was lost and came back
        std::optional<std::string> failure;  // why it couldn't come back; nothing draws
        std::vector<std::string> errors;  // the first WebGPU errors nothing caught (see Engine::getHealth)
        std::size_t errorCount = 0;
    };

    [[nodiscard]] Health getHealth() const;

    /**
     * Called when the engine needs restartIfLost(), update() and render()
     * without any input: when the GPU is lost, and once it's back. Possibly
     * from processEvents() natively, or the browser's event loop.
     */
    void setWakeCallback(std::function<void()> onWake);

    /** Turns frame timing on or off, for every engine. Off by default. */
    void setProfiling(bool enabled);

    /** Calls `onStats` once per frame every engine draws (see Engine::setStatsCallback). */
    void setStatsCallback(Gpu::Market::Engine::StatsCallback onStats);

private:
    /** Gives a new engine the session's callbacks and profiling, then `mOnSetUp`'s content. */
    void setUp();

    /** The GPU works. */
    struct Working
    {};

    /** The GPU was lost, and a new engine is on its way. */
    struct Restarting
    {};

    /** The GPU was lost and couldn't come back: nothing draws. */
    struct Failed
    {
        std::string reason;
    };

    // Null only while a new one is being created, after the GPU was lost.
    std::unique_ptr<Gpu::Market::Engine> mEngine;
    Gpu::Market::Engine::Params mParams;
    SetUpCallback mOnSetUp;
    Gpu::Type::Info mInfo;  // of the engine in use, or the last one
    std::variant<Working, Restarting, Failed> mGpu = Working{};
    std::size_t mRestarts = 0;  // times the GPU was lost and came back
    bool mProfiling = false;
    Gpu::Market::Engine::StatsCallback mOnStats{};
    std::function<void()> mOnWake{};
    // Expires with the session, for a new engine that arrives later.
    std::shared_ptr<int> mAlive = std::make_shared<int>();
};

} // namespace Viewer
