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

// The closest axis ticks get, in CSS or window pixels, so their labels fit.
constexpr double TimeTickSpacing = 120.0;
constexpr double PriceTickSpacing = 50.0;

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
    return mData.trades;
}

void App::setTrades(Core::Data::Trades trades)
{
    mData.trades = std::move(trades);
    mData.totals = Core::Data::AmountTotals(mData.trades);
    mEngine->setTrades(mData.trades);
    const std::int64_t firstUs = mData.trades.empty() ? 0 : mData.trades.timeUs.front();
    // Rounded down, also before 1970.
    mData.dayStartUs = (firstUs >= 0 ? firstUs : firstUs - DayUs + 1) / DayUs * DayUs;
    mView.price = {};
    resetView();
}

Core::Data::TradeSummary App::getViewSummary() const
{
    if (mView.trades.empty()) {
        return {};
    }
    return {
        .count = mView.trades.size(),
        .firstUs = mData.trades.timeUs[mView.trades.first],
        .lastUs = mData.trades.timeUs[mView.trades.last - 1],
        .totalAmount = mData.totals.getAmount(mView.trades),
    };
}

void App::resetView()
{
    mView.time = { .startUs = mData.dayStartUs + 12 * HourUs, .endUs = mData.dayStartUs + 13 * HourUs };
    applyView();
}

void App::showDay()
{
    mView.time = { .startUs = mData.dayStartUs, .endUs = mData.dayStartUs + DayUs };
    applyView();
}

void App::zoomAt(double x, double factor)
{
    const auto length = static_cast<double>(mView.time.getLengthUs());
    const double newLength = std::clamp(length * factor, static_cast<double>(MinViewUs), static_cast<double>(MaxViewUs));
    const double plotWidth = getPlotWidth();
    const double anchor = plotWidth > 0.0 ? (x - mLayout.insets.left) / plotWidth : 0.5;
    mView.time = mView.time.zoomed(anchor, newLength / length);
    applyView();
}

void App::panBy(double dx)
{
    if (const double plotWidth = getPlotWidth(); plotWidth > 0.0) {
        mView.time = mView.time.panned(-dx / plotWidth);
        applyView();
    }
}

void App::setMarkerScale(Gpu::Market::Type::MarkerScale scale)
{
    mEngine->setMarkerScale(scale);
}

void App::setPixelRatio(float ratio)
{
    mLayout.pixelRatio = ratio;
    mEngine->setPixelRatio(ratio);
    updateAxes();
}

void App::setPlotInsets(Gpu::Plot::Type::Insets insets)
{
    mLayout.insets = insets;
    mEngine->setPlotInsets(insets);
    updateAxes();
}

double App::getPlotWidth() const
{
    return std::max(0.0, static_cast<double>(mLayout.width) - mLayout.insets.left - mLayout.insets.right);
}

double App::getPlotHeight() const
{
    return std::max(0.0, static_cast<double>(mLayout.height) - mLayout.insets.top - mLayout.insets.bottom);
}

void App::updateAxes()
{
    // At least two ticks, so even a small plot shows a scale.
    const auto countFitting = [&](double length, double spacing) {
        return std::max<std::size_t>(2, static_cast<std::size_t>(length / (spacing * mLayout.pixelRatio)));
    };
    mView.axes = {
        .times = Core::Axis::getTimeTicks(mView.time, countFitting(getPlotWidth(), TimeTickSpacing)),
        .prices = Core::Axis::getNumberTicks(mView.price, countFitting(getPlotHeight(), PriceTickSpacing)),
    };

    Gpu::Plot::Type::Grid grid;
    for (const auto& tick : mView.axes.times) {
        grid.verticalLines.push_back(static_cast<float>(tick.fraction));
    }
    for (const auto& tick : mView.axes.prices) {
        grid.horizontalLines.push_back(static_cast<float>(tick.fraction));
    }
    mEngine->setGrid(grid);
}

void App::applyView()
{
    mView.trades = Core::Data::getTradesIn(mData.trades, mView.time);
    if (const auto price = Core::Data::getPriceRange(mData.trades, mView.trades)) {
        // Some room even for a single price, so its trades sit mid-height.
        const double padding = std::max((price->high - price->low) * PricePadding, price->high * 1e-5);
        mView.price = { .low = price->low - padding, .high = price->high + padding };
    }
    mEngine->setView({ .time = mView.time, .price = mView.price, .trades = mView.trades });
    updateAxes();
}

const Gpu::Type::Info& App::getInfo() const
{
    return mEngine->getInfo();
}

void App::resize(std::uint32_t width, std::uint32_t height)
{
    mLayout.width = width;
    mLayout.height = height;
    mEngine->resize(width, height);
    updateAxes();
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
