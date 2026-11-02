#include "Viewer/GpuSession.hpp"

#include "Core/Log.hpp"

#include <utility>

namespace Viewer {

GpuSession::GpuSession(std::unique_ptr<Gpu::Market::Engine> engine, Gpu::Market::Engine::Params params, SetUpCallback onSetUp) noexcept
    : mEngine(std::move(engine)),
      mParams(std::move(params)),
      mOnSetUp(std::move(onSetUp)),
      mInfo(mEngine->getInfo())
{
    setUp();
}

void GpuSession::restartIfLost()
{
    if (!mEngine || !mEngine->isLost()) {
        return;
    }
    Core::Log::warn("Restarting the GPU");
    // The old engine goes first: a window or canvas takes one surface at a time.
    mEngine.reset();
    mGpu = Restarting{};
    // Natively, this calls back before returning; on the web, later.
    Gpu::Market::Engine::create(mParams, [this, alive = std::weak_ptr(mAlive)](Gpu::Market::Engine::CreatedResult result) {
        if (alive.expired()) {
            return;  // the session is gone
        }
        if (!result) {
            Core::Log::error("The GPU couldn't restart: {}", result.error());
            mGpu = Failed{ .reason = std::move(result.error()) };
        } else {
            mEngine = std::move(*result);
            mGpu = Working{};
            mInfo = mEngine->getInfo();
            ++mRestarts;
            setUp();
            Core::Log::info("The GPU is back: {} · {} · {}", mInfo.device, mInfo.backend, mInfo.adapter);
        }
        if (mOnWake) {
            mOnWake();
        }
    });
}

void GpuSession::setUp()
{
    // The engine doesn't outlive the session, so neither does its callback.
    mEngine->setLostCallback([this] {
        // restartIfLost() restarts it, outside the engine's own calls.
        if (mOnWake) {
            mOnWake();
        }
    });
    mEngine->setProfiling(mProfiling);
    if (mOnStats) {
        mEngine->setStatsCallback(mOnStats);
    }
    if (auto set = mOnSetUp(*mEngine); !set) {
        Core::Log::error("{}", set.error());
        mGpu = Failed{ .reason = std::move(set.error()) };
    }
}

GpuSession::Health GpuSession::getHealth() const
{
    const auto* failed = std::get_if<Failed>(&mGpu);
    Health health{
        .restarting = std::holds_alternative<Restarting>(mGpu),
        .restarts = mRestarts,
        .failure = failed ? std::optional(failed->reason) : std::nullopt,
        .errors = {},
        .errorCount = 0,
    };
    if (mEngine) {
        health.errors = mEngine->getErrors();
        health.errorCount = mEngine->getErrorCount();
    }
    return health;
}

void GpuSession::setWakeCallback(std::function<void()> onWake)
{
    mOnWake = std::move(onWake);
}

void GpuSession::setProfiling(bool enabled)
{
    mProfiling = enabled;
    if (mEngine) {
        mEngine->setProfiling(enabled);
    }
}

void GpuSession::setStatsCallback(Gpu::Market::Engine::StatsCallback onStats)
{
    mOnStats = std::move(onStats);
    if (mEngine) {
        mEngine->setStatsCallback(mOnStats);
    }
}

} // namespace Viewer
