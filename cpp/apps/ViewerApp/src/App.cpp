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
    : mEngine(std::move(engine)),
      mSetup{ .params = std::move(params), .info = mEngine->getInfo() }
{
    setUpEngine();
}

const Core::Data::Trades& App::getTrades() const
{
    return mData.trades;
}

std::expected<void, std::string> App::setTrades(Core::Data::Trades trades)
{
    if (mEngine) {
        if (auto set = mEngine->setTrades(trades); !set) {
            return set;
        }
    }
    mData.trades = std::move(trades);
    mData.trades.shrinkToFit();
    mData.totals = Core::Data::RunningTotals(mData.trades);
    mData.prices = Core::Data::PriceTree(mData.trades);
    const std::int64_t firstUs = mData.trades.empty() ? 0 : mData.trades.timeUs.front();
    // Rounded down, also before 1970.
    mData.dayStartUs = (firstUs >= 0 ? firstUs : firstUs - DayUs + 1) / DayUs * DayUs;
    mView.price = {};
    resetView();
    return {};
}

Core::Data::TradeStats App::getViewStats() const
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

void App::zoomAt(double xPx, double factor)
{
    const auto length = static_cast<double>(mView.time.getLengthUs());
    const double newLength = std::clamp(length * factor, static_cast<double>(MinViewUs), static_cast<double>(MaxViewUs));
    const double plotWidthPx = getPlotWidthPx();
    const double anchor = plotWidthPx > 0.0 ? (xPx - mLayout.insets.left) / plotWidthPx : 0.5;
    mView.time = mView.time.zoomed(anchor, newLength / length);
    applyView();
}

void App::panBy(double dxPx)
{
    if (const double plotWidthPx = getPlotWidthPx(); plotWidthPx > 0.0) {
        mView.time = mView.time.panned(-dxPx / plotWidthPx);
        applyView();
    }
}

void App::setMarkerScale(Gpu::Market::Type::MarkerScale scale)
{
    mMarkerScale = scale;
    if (mEngine) {
        mEngine->setMarkerScale(scale);
    }
    updatePick();
}

void App::setTheme(const Gpu::Market::Type::Theme& theme)
{
    mTheme = theme;
    if (mEngine) {
        mEngine->setTheme(theme);
    }
}

Gpu::Market::Type::Legend App::getLegend() const
{
    return Gpu::Market::Style::getLegend(mMarkerScale, mTheme);
}

void App::setPixelRatio(float ratio)
{
    mLayout.pixelRatio = ratio;
    if (mEngine) {
        mEngine->setPixelRatio(ratio);
    }
    updateAxes();
    updateSummaries();
    updatePick();
}

void App::setPlotInsets(Gpu::Plot::Type::Insets insets)
{
    mLayout.insets = insets;
    if (mEngine) {
        mEngine->setPlotInsets(insets);
    }
    updateAxes();
    updateSummaries();
    updatePick();
}

double App::getPlotWidthPx() const
{
    return std::max(0.0, static_cast<double>(mLayout.surfaceWidthPx) - mLayout.insets.left - mLayout.insets.right);
}

double App::getPlotHeightPx() const
{
    return std::max(0.0, static_cast<double>(mLayout.surfaceHeightPx) - mLayout.insets.top - mLayout.insets.bottom);
}

void App::updateAxes()
{
    // At least two ticks, so even a small plot shows a scale.
    const auto countFitting = [&](double lengthPx, double spacingCssPx) {
        return std::max<std::size_t>(2, static_cast<std::size_t>(lengthPx / (spacingCssPx * mLayout.pixelRatio)));
    };
    mView.axes = {
        .times = Core::Plot::Axis::getTimeTicks(mView.time, countFitting(getPlotWidthPx(), TimeTickSpacingCssPx)),
        .prices = Core::Plot::Axis::getNumberTicks(mView.price, countFitting(getPlotHeightPx(), PriceTickSpacingCssPx)),
    };

    Gpu::Plot::Type::Grid grid;
    for (const auto& tick : mView.axes.times) {
        grid.verticalLines.push_back(static_cast<float>(tick.fraction));
    }
    for (const auto& tick : mView.axes.prices) {
        grid.horizontalLines.push_back(static_cast<float>(tick.fraction));
    }
    if (mEngine) {
        mEngine->setGrid(grid);
    }
}

void App::applyView()
{
    mView.trades = Core::Data::getTradesIn(mData.trades, mView.time);
    if (const auto price = mData.prices.getRange(mView.trades)) {
        // Some room even for a single price, so its trades sit mid-height.
        const double padding = std::max((price->high - price->low) * PricePadding, price->high * 1e-5);
        mView.price = { .low = price->low - padding, .high = price->high + padding };
    }
    if (mEngine) {
        mEngine->setView({ .time = mView.time, .price = mView.price, .trades = mView.trades });
    }
    updateAxes();
    updateSummaries();
    updatePick();
}

void App::setSummariesAllowed(bool allowed)
{
    mSummaries.allowed = allowed;
    updateSummaries();
    updatePick();
}

void App::setSummaryThreshold(double tradesPerColumn)
{
    mSummaries.threshold = tradesPerColumn;
    updateSummaries();
    updatePick();
}

void App::updateSummaries()
{
    // One column per CSS or window pixel.
    const double across = std::round(getPlotWidthPx() / mLayout.pixelRatio);
    const bool crowded = mSummaries.allowed && across >= 1.0
                         && static_cast<double>(mView.trades.size()) > mSummaries.threshold * across;
    if (!crowded) {
        if (mSummaries.across > 0) {
            mSummaries.across = 0;
            mSummaries.columns.clear();
            showColumns();
        }
        return;
    }
    mSummaries.across = static_cast<std::uint32_t>(across);
    mSummaries.columns = Core::Data::summarizeColumns(mData.trades, mData.totals, mData.prices, mView.time, mSummaries.across);
    showColumns();
}

void App::showColumns()
{
    if (!mEngine) {
        return;
    }
    if (mSummaries.across == 0) {
        mEngine->setColumns(std::nullopt);
        return;
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
    mEngine->setColumns(columns);
}

Core::Plot::Mapping App::getPlotMapping() const
{
    return { .time = mView.time, .price = mView.price, .width = getPlotWidthPx(), .height = getPlotHeightPx() };
}

void App::setPointer(double xPx, double yPx)
{
    mCursor.onSurface = true;
    mCursor.xPx = xPx;
    mCursor.yPx = yPx;
    updatePick();
}

void App::clearPointer()
{
    mCursor.onSurface = false;
    updatePick();
}

std::optional<App::PickedTrade> App::getPickedTrade() const
{
    if (!mCursor.trade) {
        return std::nullopt;
    }
    const std::size_t i = *mCursor.trade;
    const auto plot = getPlotMapping();
    return PickedTrade{
        .index = i,
        .timeUs = mData.trades.timeUs[i],
        .price = mData.trades.price[i],
        .amount = mData.trades.amount[i],
        .side = mData.trades.side[i],
        .centerXPx = mLayout.insets.left + plot.getX(mData.trades.timeUs[i]),
        .centerYPx = mLayout.insets.top + plot.getY(mData.trades.price[i]),
    };
}

std::optional<App::PickedColumn> App::getPickedColumn() const
{
    if (!mCursor.column) {
        return std::nullopt;
    }
    const auto& column = mSummaries.columns[*mCursor.column];
    const auto bar = Core::Plot::getColumnBar(column, mSummaries.across, getPlotMapping(),
                                              Gpu::Market::Style::ColumnMinHeightCssPx * mLayout.pixelRatio);
    const std::int64_t length = mView.time.getLengthUs();
    const auto across = static_cast<std::int64_t>(mSummaries.across);
    const auto index = static_cast<std::int64_t>(column.index);
    return PickedColumn{
        .summary = column,
        .time = { .startUs = mView.time.startUs + index * length / across, .endUs = mView.time.startUs + (index + 1) * length / across },
        .centerXPx = mLayout.insets.left + bar.centerX,
        .topPx = mLayout.insets.top + bar.top,
        .bottomPx = mLayout.insets.top + bar.bottom,
    };
}

void App::updatePick()
{
    std::optional<std::size_t> trade;
    std::optional<std::size_t> column;
    if (mCursor.onSurface) {
        const double ratio = mLayout.pixelRatio;
        const auto plot = getPlotMapping();
        const double pointerXPx = mCursor.xPx - mLayout.insets.left;
        const double pointerYPx = mCursor.yPx - mLayout.insets.top;
        if (mSummaries.across > 0) {
            column = Core::Plot::pickColumn(mSummaries.columns, mSummaries.across, plot, pointerXPx, pointerYPx, PickRadiusCssPx * ratio,
                                            Gpu::Market::Style::ColumnMinHeightCssPx * ratio);
        } else {
            const Core::Plot::PickRule rule{ .pickRadius = PickRadiusCssPx * ratio, .maxMarkerRadius = Gpu::Market::Style::MarkerMaxRadiusCssPx * ratio };
            trade = Core::Plot::pickTrade(mData.trades, mView.trades, plot, pointerXPx, pointerYPx, rule,
                                          [&](double amount) { return Gpu::Market::Style::getMarkerRadiusCssPx(amount, mMarkerScale) * ratio; });
        }
    }
    if (trade != mCursor.trade) {
        mCursor.trade = trade;
        if (mEngine) {
            mEngine->setHighlightedTrade(trade);
        }
    }
    // A position stays right when the columns are replaced: it's into the new ones.
    if (column != mCursor.column) {
        mCursor.column = column;
        if (mEngine) {
            mEngine->setHighlightedColumn(column);
        }
    }
}

const Gpu::Type::Info& App::getInfo() const
{
    return mSetup.info;
}

void App::resize(std::uint32_t widthPx, std::uint32_t heightPx)
{
    // The native loop calls this every iteration.
    if (widthPx == mLayout.surfaceWidthPx && heightPx == mLayout.surfaceHeightPx) {
        return;
    }
    mLayout.surfaceWidthPx = widthPx;
    mLayout.surfaceHeightPx = heightPx;
    if (mEngine) {
        mEngine->resize(widthPx, heightPx);
    }
    updateAxes();
    updateSummaries();
    updatePick();
}

void App::update(double nowMs)
{
    if (mEngine && mEngine->isLost()) {
        restartGpu();
    }
    if (mEngine) {
        mEngine->update(nowMs);
    }
}

bool App::needsRender() const
{
    return mEngine && mEngine->needsRender();
}

void App::render()
{
    if (mEngine) {
        mEngine->render();
    }
}

void App::setProfiling(bool enabled)
{
    mSetup.profiling = enabled;
    if (mEngine) {
        mEngine->setProfiling(enabled);
    }
}

void App::setStatsCallback(Gpu::Market::Engine::StatsCallback onStats)
{
    mSetup.onStats = std::move(onStats);
    if (mEngine) {
        mEngine->setStatsCallback(mSetup.onStats);
    }
}

void App::readPixels(Gpu::Market::Engine::PixelsCallback onDone)
{
    if (!mEngine) {
        onDone(std::unexpected("The GPU is restarting"));
        return;
    }
    mEngine->readPixels(std::move(onDone));
}

#ifndef __EMSCRIPTEN__
void App::processEvents()
{
    if (mEngine) {
        mEngine->processEvents();
    }
}
#endif

App::GpuHealth App::getGpuHealth() const
{
    const auto* failed = std::get_if<GpuFailed>(&mSetup.gpu);
    GpuHealth health{
        .restarting = std::holds_alternative<GpuRestarting>(mSetup.gpu),
        .restarts = mSetup.restarts,
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

App::MemoryStats App::getMemoryStats() const
{
    return {
        .tradesBytes = mData.trades.getBytes(),
        .totalsBytes = mData.totals.getBytes(),
        .priceTreeBytes = mData.prices.getBytes(),
        .summariesBytes = mSummaries.columns.capacity() * sizeof(Core::Data::ColumnSummary),
        .gpuBytes = mEngine ? mEngine->getGpuBytes() : 0,
    };
}

void App::setWakeCallback(std::function<void()> onWake)
{
    mSetup.onWake = std::move(onWake);
}

void App::restartGpu()
{
    Core::Log::warn("Restarting the GPU");
    // The old engine goes first: a window or canvas takes one surface at a time.
    mEngine.reset();
    mSetup.gpu = GpuRestarting{};
    // Natively, this calls back before returning; on the web, later.
    Gpu::Market::Engine::create(mSetup.params,
        [this, alive = std::weak_ptr(mSetup.alive)](Gpu::Market::Engine::CreatedResult result) {
            if (alive.expired()) {
                return;  // the app is gone
            }
            if (!result) {
                Core::Log::error("The GPU couldn't restart: {}", result.error());
                mSetup.gpu = GpuFailed{ .reason = std::move(result.error()) };
            } else {
                mEngine = std::move(*result);
                mSetup.gpu = GpuWorking{};
                mSetup.info = mEngine->getInfo();
                ++mSetup.restarts;
                setUpEngine();
                Core::Log::info("The GPU is back: {} · {} · {}", mSetup.info.device, mSetup.info.backend, mSetup.info.adapter);
            }
            if (mSetup.onWake) {
                mSetup.onWake();
            }
        });
}

void App::setUpEngine()
{
    // The engine doesn't outlive the app, so neither does its callback.
    mEngine->setLostCallback([this] {
        // update() restarts it, outside the engine's own calls.
        if (mSetup.onWake) {
            mSetup.onWake();
        }
    });
    mEngine->setProfiling(mSetup.profiling);
    if (mSetup.onStats) {
        mEngine->setStatsCallback(mSetup.onStats);
    }
    mEngine->resize(mLayout.surfaceWidthPx, mLayout.surfaceHeightPx);
    mEngine->setPixelRatio(mLayout.pixelRatio);
    mEngine->setPlotInsets(mLayout.insets);
    mEngine->setTheme(mTheme);
    mEngine->setMarkerScale(mMarkerScale);
    if (auto set = mEngine->setTrades(mData.trades); !set) {
        // Only if the new GPU holds fewer than the old one did.
        Core::Log::error("{}", set.error());
        mSetup.gpu = GpuFailed{ .reason = std::move(set.error()) };
        return;
    }
    mEngine->setView({ .time = mView.time, .price = mView.price, .trades = mView.trades });
    updateAxes();
    showColumns();
    mEngine->setHighlightedTrade(mCursor.trade);
    mEngine->setHighlightedColumn(mCursor.column);
}

} // namespace Viewer
