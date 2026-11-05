#include "Viewer/App.hpp"

#include "Core/Plot/Pick.hpp"
#include "Core/Data/Synthetic.hpp"
#include "Gpu/Market/Style.hpp"

#include "Core/Log.hpp"

#include <algorithm>
#include <cmath>
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

// How far from the pointer a trade's center can be picked, in CSS or window pixels.
constexpr double PickRadiusCssPx = 24.0;

// The closest axis ticks get, in CSS or window pixels, so their labels fit.
constexpr double TimeTickSpacingCssPx = 120.0;
constexpr double PriceTickSpacingCssPx = 50.0;

} // namespace

Core::Data::Trades generateSyntheticDay()
{
    return Core::Data::generateSyntheticTrades(DayStartUs, DayStartUs + DayUs, SyntheticSeed);
}

void App::create(const Gpu::Market::Engine::Params& params, Callback onDone)
{
    Gpu::Market::Engine::create(params, [params, onDone = std::move(onDone)](Gpu::Market::Engine::CreatedResult result) {
        if (!result) {
            onDone(std::unexpected(std::move(result.error())));
            return;
        }
        onDone(std::make_unique<App>(std::move(*result), params));
    });
}

App::App(std::unique_ptr<Gpu::Market::Engine> engine, Gpu::Market::Engine::Params params) noexcept
    : mGpu(std::move(engine), std::move(params), [this](Gpu::Market::Engine& newEngine) { return setUpEngine(newEngine); })
{}

const Core::Data::Trades& App::getTrades() const
{
    return mData.getTrades();
}

std::expected<void, std::string> App::setTrades(Core::Data::Trades&& trades)
{
    if (auto* engine = mGpu.getEngine()) {
        if (auto set = engine->setTrades(trades); !set) {
            return set;
        }
    }
    mData = Core::Data::PreprocessedTrades(std::move(trades));
    mSettings.markerSizing = Gpu::Market::Style::fitMarkerSizing(mSettings.markerSizing.scale, mData.getTrades());
    mSettings.view.price = {};
    resetView();
    return {};
}

Core::Data::TradeStats App::getViewStats() const
{
    const auto& indices = mSettings.view.trades;
    if (indices.empty()) {
        return {};
    }
    const auto& timeUs = mData.getTrades().timeUs;
    return {
        .count = indices.size(),
        .firstUs = timeUs[indices.first],
        .lastUs = timeUs[indices.last - 1],
        .totalAmount = mData.getAmount(indices),
    };
}

std::int64_t App::getDayStartUs() const
{
    const auto& timeUs = mData.getTrades().timeUs;
    const std::int64_t firstUs = timeUs.empty() ? 0 : timeUs.front();
    // Rounded down, also before 1970.
    return (firstUs >= 0 ? firstUs : firstUs - DayUs + 1) / DayUs * DayUs;
}

void App::resetView()
{
    const std::int64_t dayStartUs = getDayStartUs();
    mSettings.view.time = { .startUs = dayStartUs + 12 * HourUs, .endUs = dayStartUs + 13 * HourUs };
    applyView();
}

void App::showDay()
{
    const std::int64_t dayStartUs = getDayStartUs();
    mSettings.view.time = { .startUs = dayStartUs, .endUs = dayStartUs + DayUs };
    applyView();
}

void App::zoomAt(double xPx, double factor)
{
    auto& time = mSettings.view.time;
    const auto length = static_cast<double>(time.getLengthUs());
    const double newLength = std::clamp(length * factor, static_cast<double>(MinViewUs), static_cast<double>(MaxViewUs));
    const double plotWidthPx = getPlotWidthPx();
    const double anchor = plotWidthPx > 0.0 ? (xPx - mSettings.insets.left) / plotWidthPx : 0.5;
    time = time.zoomed(anchor, newLength / length);
    applyView();
}

void App::panBy(double dxPx)
{
    if (const double plotWidthPx = getPlotWidthPx(); plotWidthPx > 0.0) {
        mSettings.view.time = mSettings.view.time.panned(-dxPx / plotWidthPx);
        applyView();
    }
}

void App::setMarkerScale(Gpu::Market::Type::MarkerScale scale)
{
    mSettings.markerSizing.scale = scale;
    updatePick();
    syncSettings();
}

void App::setTheme(const Gpu::Market::Type::Theme& theme)
{
    mSettings.theme = theme;
    syncSettings();
}

Gpu::Market::Type::Legend App::getLegend() const
{
    return Gpu::Market::Style::getLegend(mSettings.markerSizing, mSettings.theme);
}

void App::setPixelRatio(float ratio)
{
    mSettings.pixelRatio = ratio;
    updateAxes();
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::setPlotInsets(Gpu::Plot::Type::Insets insets)
{
    mSettings.insets = insets;
    updateAxes();
    updateSummaries();
    updatePick();
    syncSettings();
}

double App::getPlotWidthPx() const
{
    return std::max(0.0, static_cast<double>(mSettings.widthPx) - mSettings.insets.left - mSettings.insets.right);
}

double App::getPlotHeightPx() const
{
    return std::max(0.0, static_cast<double>(mSettings.heightPx) - mSettings.insets.top - mSettings.insets.bottom);
}

void App::updateAxes()
{
    // At least two ticks, so even a small plot shows a scale.
    const auto countFitting = [&](double lengthPx, double spacingCssPx) {
        return std::max<std::size_t>(2, static_cast<std::size_t>(lengthPx / (spacingCssPx * mSettings.pixelRatio)));
    };
    mAxes = {
        .times = Core::Plot::Axis::getTimeTicks(mSettings.view.time, countFitting(getPlotWidthPx(), TimeTickSpacingCssPx)),
        .prices = Core::Plot::Axis::getNumberTicks(mSettings.view.price, countFitting(getPlotHeightPx(), PriceTickSpacingCssPx)),
    };
    mSettings.grid = {};
    for (const auto& tick : mAxes.times) {
        mSettings.grid.verticalLines.push_back(static_cast<float>(tick.fraction));
    }
    for (const auto& tick : mAxes.prices) {
        mSettings.grid.horizontalLines.push_back(static_cast<float>(tick.fraction));
    }
}

void App::applyView()
{
    auto& view = mSettings.view;
    view.trades = Core::Data::getTradesIn(mData.getTrades(), view.time);
    if (const auto price = mData.getPriceRange(view.trades)) {
        // Some room even for a single price, so its trades sit mid-height.
        const double padding = std::max((price->high - price->low) * PricePadding, price->high * 1e-5);
        view.price = { .low = price->low - padding, .high = price->high + padding };
    }
    updateAxes();
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::setSummariesAllowed(bool allowed)
{
    mSummaries.allowed = allowed;
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::setSummaryThreshold(double tradesPerColumn)
{
    mSummaries.threshold = tradesPerColumn;
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::updateSummaries()
{
    // One column per CSS or window pixel.
    const double across = std::round(getPlotWidthPx() / mSettings.pixelRatio);
    const bool crowded = mSummaries.allowed && across >= 1.0
                         && static_cast<double>(mSettings.view.trades.size()) > mSummaries.threshold * across;
    if (!crowded) {
        if (mSummaries.across > 0) {
            mSummaries.across = 0;
            mSummaries.columns.clear();
            syncColumns();
        }
        return;
    }
    mSummaries.across = static_cast<std::uint32_t>(across);
    mSummaries.columns = Core::Data::summarizeColumns(mData, mSettings.view.time, mSummaries.across);
    syncColumns();
}

void App::syncSettings()
{
    if (auto* engine = mGpu.getEngine()) {
        engine->setSettings(mSettings);
    }
}

void App::syncColumns()
{
    if (auto* engine = mGpu.getEngine()) {
        engine->setColumns(getEngineColumns());
    }
}

std::optional<Gpu::Market::Type::Columns> App::getEngineColumns() const
{
    if (mSummaries.across == 0) {
        return std::nullopt;
    }
    Gpu::Market::Type::Columns columns{ .across = mSummaries.across, .columns = {} };
    columns.columns.reserve(mSummaries.columns.size());
    for (const auto& column : mSummaries.columns) {
        columns.columns.push_back({
            .index = static_cast<std::uint32_t>(column.index),
            .price = column.price,
            .buyAmount = column.buyAmount,
            .sellAmount = column.sellAmount,
        });
    }
    return columns;
}

Core::Plot::Mapping App::getPlotMapping() const
{
    return { .time = mSettings.view.time, .price = mSettings.view.price, .width = getPlotWidthPx(), .height = getPlotHeightPx() };
}

void App::setPointer(double xPx, double yPx)
{
    mCursor.onSurface = true;
    mCursor.xPx = xPx;
    mCursor.yPx = yPx;
    updatePick();
    syncSettings();
}

void App::clearPointer()
{
    mCursor.onSurface = false;
    updatePick();
    syncSettings();
}

std::optional<App::PickedTrade> App::getPickedTrade() const
{
    if (!mSettings.highlightedTrade) {
        return std::nullopt;
    }
    const std::size_t i = *mSettings.highlightedTrade;
    const auto plot = getPlotMapping();
    const auto& trades = mData.getTrades();
    return PickedTrade{
        .index = i,
        .timeUs = trades.timeUs[i],
        .price = trades.price[i],
        .amount = trades.amount[i],
        .side = trades.side[i],
        .centerXPx = mSettings.insets.left + plot.getX(trades.timeUs[i]),
        .centerYPx = mSettings.insets.top + plot.getY(trades.price[i]),
    };
}

std::optional<App::PickedColumn> App::getPickedColumn() const
{
    if (!mSettings.highlightedColumn) {
        return std::nullopt;
    }
    const auto& column = mSummaries.columns[*mSettings.highlightedColumn];
    const auto bar = Core::Plot::getColumnBar(column, mSummaries.across, getPlotMapping(),
                                              Gpu::Market::Style::ColumnMinHeightCssPx * mSettings.pixelRatio);
    const auto& time = mSettings.view.time;
    const std::int64_t length = time.getLengthUs();
    const auto across = static_cast<std::int64_t>(mSummaries.across);
    const auto index = static_cast<std::int64_t>(column.index);
    return PickedColumn{
        .summary = column,
        .time = { .startUs = time.startUs + index * length / across, .endUs = time.startUs + (index + 1) * length / across },
        .centerXPx = mSettings.insets.left + bar.centerX,
        .topPx = mSettings.insets.top + bar.top,
        .bottomPx = mSettings.insets.top + bar.bottom,
    };
}

void App::updatePick()
{
    std::optional<std::size_t> trade;
    std::optional<std::size_t> column;
    if (mCursor.onSurface) {
        const double ratio = mSettings.pixelRatio;
        const auto plot = getPlotMapping();
        const double pointerXPx = mCursor.xPx - mSettings.insets.left;
        const double pointerYPx = mCursor.yPx - mSettings.insets.top;
        if (mSummaries.across > 0) {
            column = Core::Plot::pickColumn(mSummaries.columns, mSummaries.across, plot, pointerXPx, pointerYPx, PickRadiusCssPx * ratio,
                                            Gpu::Market::Style::ColumnMinHeightCssPx * ratio);
        } else {
            const Core::Plot::PickRule rule{ .pickRadius = PickRadiusCssPx * ratio, .maxMarkerRadius = Gpu::Market::Style::MarkerMaxRadiusCssPx * ratio };
            trade = Core::Plot::pickTrade(mData.getTrades(), mSettings.view.trades, plot, pointerXPx, pointerYPx, rule,
                                          [&](double amount) { return mSettings.markerSizing.getRadiusCssPx(amount) * ratio; });
        }
    }
    // A column's position stays right when the columns are replaced: it's into the new ones.
    mSettings.highlightedTrade = trade;
    mSettings.highlightedColumn = column;
}

const Gpu::Type::Info& App::getInfo() const
{
    return mGpu.getInfo();
}

void App::resize(std::uint32_t widthPx, std::uint32_t heightPx)
{
    // The native loop calls this every iteration.
    if (widthPx == mSettings.widthPx && heightPx == mSettings.heightPx) {
        return;
    }
    mSettings.widthPx = widthPx;
    mSettings.heightPx = heightPx;
    updateAxes();
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::update(double nowMs)
{
    mGpu.restartIfLost();
    if (auto* engine = mGpu.getEngine()) {
        engine->update(nowMs);
    }
}

bool App::needsRender() const
{
    const auto* engine = mGpu.getEngine();
    return engine && engine->needsRender();
}

void App::render()
{
    if (auto* engine = mGpu.getEngine()) {
        engine->render();
    }
}

void App::setProfiling(bool enabled)
{
    mGpu.setProfiling(enabled);
}

void App::setStatsCallback(Gpu::Market::Engine::StatsCallback onStats)
{
    mGpu.setStatsCallback(std::move(onStats));
}

void App::readPixels(Gpu::Market::Engine::PixelsCallback onDone)
{
    auto* engine = mGpu.getEngine();
    if (!engine) {
        onDone(std::unexpected("The GPU is restarting"));
        return;
    }
    engine->readPixels(std::move(onDone));
}

#ifndef __EMSCRIPTEN__
void App::processEvents()
{
    if (auto* engine = mGpu.getEngine()) {
        engine->processEvents();
    }
}
#endif

App::GpuHealth App::getGpuHealth() const
{
    return mGpu.getHealth();
}

App::MemoryStats App::getMemoryStats() const
{
    const auto bytes = mData.getBytes();
    const auto* engine = mGpu.getEngine();
    return {
        .tradesBytes = bytes.trades,
        .totalsBytes = bytes.totals,
        .priceTreeBytes = bytes.priceTree,
        .summariesBytes = mSummaries.columns.capacity() * sizeof(Core::Data::ColumnSummary),
        .gpuBytes = engine ? engine->getGpuBytes() : 0,
    };
}

void App::setWakeCallback(std::function<void()> onWake)
{
    mGpu.setWakeCallback(std::move(onWake));
}

std::expected<void, std::string> App::setUpEngine(Gpu::Market::Engine& engine)
{
    // Fails only if the new GPU holds fewer than the old one did.
    if (auto set = engine.setTrades(mData.getTrades()); !set) {
        return set;
    }
    engine.setColumns(getEngineColumns());
    engine.setSettings(mSettings);
    return {};
}

} // namespace Viewer
