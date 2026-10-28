#include "App.hpp"

#include "Core/Data/Synthetic.hpp"

#include <cstdint>
#include <utility>

namespace Viewer {

namespace {

constexpr std::int64_t DayStartUs = 1'575'158'400'000'000;
constexpr std::int64_t DayUs = 86'400'000'000;
constexpr std::uint64_t SyntheticSeed = 1;

} // namespace

Core::Data::Trades syntheticDay()
{
    return Core::Data::syntheticTrades(DayStartUs, DayStartUs + DayUs, SyntheticSeed);
}

void App::create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone)
{
    Gpu::Market::Engine::create(target, powerPreference, [onDone = std::move(onDone)](Gpu::Market::Engine::CreatedResult result) {
        if (!result) {
            onDone(std::unexpected(std::move(result.error())));
            return;
        }
        onDone(std::make_unique<App>(std::move(*result)));
    });
}

App::App(std::unique_ptr<Gpu::Market::Engine> engine) noexcept
    : mEngine(std::move(engine))
{}

const Core::Data::Trades& App::trades() const
{
    return mTrades;
}

void App::setTrades(Core::Data::Trades trades)
{
    mTrades = std::move(trades);
}

const Gpu::Type::Info& App::info() const
{
    return mEngine->info();
}

void App::resize(std::uint32_t width, std::uint32_t height)
{
    mEngine->resize(width, height);
}

void App::update(double nowMs)
{
    mEngine->update(nowMs);
}

bool App::needsRender() const
{
    return mEngine->needsRender();
}

void App::render()
{
    mEngine->render();
}

void App::setProfiling(bool enabled)
{
    mEngine->setProfiling(enabled);
}

void App::setStatsCallback(Gpu::Market::Engine::StatsCallback onStats)
{
    mEngine->setStatsCallback(std::move(onStats));
}

#ifndef __EMSCRIPTEN__
void App::processEvents()
{
    mEngine->processEvents();
}
#endif

} // namespace Viewer
