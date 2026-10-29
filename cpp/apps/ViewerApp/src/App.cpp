#include "App.hpp"

#include "Core/Data/Synthetic.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace Viewer {

namespace {

constexpr std::int64_t DayStartUs = 1'575'158'400'000'000;
constexpr std::int64_t DayUs = 86'400'000'000;
constexpr std::int64_t HourUs = 3'600'000'000;
constexpr std::uint64_t SyntheticSeed = 1;

// How far the time range can zoom.
constexpr std::int64_t MinViewUs = 1'000;
constexpr std::int64_t MaxViewUs = 7 * DayUs;

// Space above and below the visible trades, as a share of their price range.
constexpr double PricePadding = 0.05;

} // namespace

Core::Data::Trades generateSyntheticDay()
{
    return Core::Data::generateSyntheticTrades(DayStartUs, DayStartUs + DayUs, SyntheticSeed);
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

const Core::Data::Trades& App::getTrades() const
{
    return mTrades;
}

void App::setTrades(Core::Data::Trades trades)
{
    mTrades = std::move(trades);
    mTotals = Core::Data::AmountTotals(mTrades);
    mEngine->setTrades(mTrades);
    const std::int64_t firstUs = mTrades.empty() ? 0 : mTrades.timeUs.front();
    // Rounded down, also before 1970.
    mDayStartUs = (firstUs >= 0 ? firstUs : firstUs - DayUs + 1) / DayUs * DayUs;
    mPrice = {};
    resetView();
}

Core::Data::TradeSummary App::getViewSummary() const
{
    if (mTradesInView.empty()) {
        return {};
    }
    return {
        .count = mTradesInView.size(),
        .firstUs = mTrades.timeUs[mTradesInView.first],
        .lastUs = mTrades.timeUs[mTradesInView.last - 1],
        .totalAmount = mTotals.getAmount(mTradesInView),
    };
}

void App::resetView()
{
    mView = { .startUs = mDayStartUs + 12 * HourUs, .endUs = mDayStartUs + 13 * HourUs };
    applyView();
}

void App::showDay()
{
    mView = { .startUs = mDayStartUs, .endUs = mDayStartUs + DayUs };
    applyView();
}

void App::zoomAt(double x, double factor)
{
    const auto length = static_cast<double>(mView.getLengthUs());
    const double newLength = std::clamp(length * factor, static_cast<double>(MinViewUs), static_cast<double>(MaxViewUs));
    const double anchor = mWidth > 0 ? x / mWidth : 0.5;
    mView = mView.zoomed(anchor, newLength / length);
    applyView();
}

void App::panBy(double dx)
{
    if (mWidth > 0) {
        mView = mView.panned(-dx / mWidth);
        applyView();
    }
}

void App::setMarkerScale(Gpu::Market::Type::MarkerScale scale)
{
    mEngine->setMarkerScale(scale);
}

void App::setPixelRatio(float ratio)
{
    mEngine->setPixelRatio(ratio);
}

void App::applyView()
{
    mTradesInView = Core::Data::getTradesIn(mTrades, mView);
    if (const auto price = Core::Data::getPriceRange(mTrades, mTradesInView)) {
        // Some room even for a single price, so its trades sit mid-height.
        const double padding = std::max((price->high - price->low) * PricePadding, price->high * 1e-5);
        mPrice = { .low = price->low - padding, .high = price->high + padding };
    }
    mEngine->setView({ .time = mView, .price = mPrice, .trades = mTradesInView });
}

const Gpu::Type::Info& App::getInfo() const
{
    return mEngine->getInfo();
}

void App::resize(std::uint32_t width, std::uint32_t height)
{
    mWidth = width;
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
