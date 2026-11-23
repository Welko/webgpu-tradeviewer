#include "Viewer/App.hpp"

#include "Core/Data/Synthetic.hpp"
#include "Gpu/Market/Style.hpp"

#include "Core/Log.hpp"

#include <cstdint>
#include <utility>

namespace Viewer {

namespace {

constexpr std::int64_t DayStartUs = 1'575'158'400'000'000;
constexpr std::int64_t DayUs = 86'400'000'000;
constexpr std::int64_t HourUs = 3'600'000'000;
constexpr std::uint64_t SyntheticSeed = 1;

constexpr double TelemetryGpuTimingProbability = 1.0 / 30.0;

}  // namespace

Core::Data::Trades generateSyntheticDay()
{
    return Core::Data::generateSyntheticTrades(DayStartUs, DayStartUs + DayUs, SyntheticSeed);
}

void App::create(const Gpu::Market::Engine::Params& params, Callback onDone)
{
    Gpu::Market::Engine::create(params, [params, onDone = std::move(onDone)](Gpu::Market::Engine::CreatedResult result) {
        if (!result)
        {
            onDone(std::unexpected(std::move(result.error())));
            return;
        }
        onDone(std::unique_ptr<App>(new App(std::move(*result), params)));
    });
}

App::App(std::unique_ptr<Gpu::Market::Engine> engine, Gpu::Market::Engine::Params params) noexcept
    : mGpu(std::move(engine), params, [this](Gpu::Market::Engine& newEngine) { return setUpEngine(newEngine); })
{
    mGpu.setStatsCallback([this](const Gpu::Type::FrameStats& stats) {
        mTelemetry.recordFrameTimes(stats);
        if (mOnStats)
        {
            mOnStats(stats);
        }
    });
}

std::expected<void, std::string> App::setTrades(Core::Data::Trades&& trades)
{
    if (auto* engine = mGpu.getEngine())
    {
        if (auto set = engine->setTrades(trades); !set)
        {
            return set;
        }
    }
    mData = Core::Data::PreprocessedTrades(std::move(trades));
    mMarkerSizing = Gpu::Market::Style::fitMarkerSizing(mMarkerSizing.scale, mData.getTrades());
    // No price range from the old trades.
    // Without trades, the time range stays.
    const auto time = getInitialView().value_or(mView.getTimeRange());
    mView = {};
    // Not setView(): new trades aren't the user changing the view.
    mView.setTimeRange(mData, time);
    applyView();
    return {};
}

void App::setSize(std::uint32_t widthPx, std::uint32_t heightPx)
{
    // The native loop calls this every iteration.
    if (widthPx == mLayout.widthPx && heightPx == mLayout.heightPx)
    {
        return;
    }
    mLayout.widthPx = widthPx;
    mLayout.heightPx = heightPx;
    updateAxes();
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::setPixelRatio(float ratio)
{
    mLayout.pixelRatio = ratio;
    updateAxes();
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::setPlotInsets(Gpu::Plot::Type::Insets insets)
{
    mLayout.insets = insets;
    updateAxes();
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::setView(Core::TimeRange time)
{
    mView.setTimeRange(mData, time);
    applyView();
    mTelemetry.recordViewChange(time.getLengthUs(), time == getInitialView(), time == Core::getUtcDay(time.startUs));
}

std::optional<Core::TimeRange> App::getInitialView() const
{
    if (mData.getTrades().empty())
    {
        return std::nullopt;
    }
    const std::int64_t dayStartUs = Core::getUtcDay(getTrades().timeUs.front()).startUs;
    return Core::TimeRange{ .startUs = dayStartUs + 12 * HourUs, .endUs = dayStartUs + 13 * HourUs };
}

void App::zoomAt(double xPx, double factor)
{
    mView.zoomAt(mData, mLayout, xPx, factor);
    applyView();
    mTelemetry.recordViewChange(mView.getTimeRange().getLengthUs(), false, false);
}

void App::panBy(double dxPx)
{
    mView.panBy(mData, mLayout, dxPx);
    applyView();
    mTelemetry.recordViewChange(mView.getTimeRange().getLengthUs(), false, false);
}

void App::applyView()
{
    updateAxes();
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::setSummariesAllowed(bool allowed)
{
    mSummaries.setAllowed(allowed);
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::setSummaryThreshold(double tradesPerColumn)
{
    mSummaries.setThreshold(tradesPerColumn);
    updateSummaries();
    updatePick();
    syncSettings();
}

void App::setTheme(const Gpu::Market::Type::Theme& theme)
{
    mTheme = theme;
    syncSettings();
}

void App::setMarkerScale(Gpu::Market::Type::MarkerScale scale)
{
    mMarkerSizing.scale = scale;
    updatePick();
    syncSettings();
}

Gpu::Market::Type::Legend App::getLegend() const
{
    return Gpu::Market::Style::getLegend(mMarkerSizing, mTheme);
}

void App::setPointer(double xPx, double yPx)
{
    mPointer.setPosition(xPx, yPx);
    updatePick();
    syncSettings();
}

void App::clearPointer()
{
    mPointer.clear();
    updatePick();
    syncSettings();
}

void App::updateAxes()
{
    mAxes.update(mView, mLayout);
}

void App::updateSummaries()
{
    if (!mSummaries.update(mData, mView, mLayout))
    {
        return;
    }
    if (auto* engine = mGpu.getEngine())
    {
        engine->setColumns(mSummaries.getEngineColumns());
    }
}

void App::updatePick()
{
    const auto trade = mPointer.getHighlightedTrade();
    const auto column = mPointer.getHighlightedColumn();
    mPointer.update(mData, mView, mSummaries, mLayout, mMarkerSizing);
    const auto newTrade = mPointer.getHighlightedTrade();
    const auto newColumn = mPointer.getHighlightedColumn();
    if ((newTrade && newTrade != trade) || (newColumn && newColumn != column))
    {
        mTelemetry.recordPick();
    }
}

void App::syncSettings()
{
    if (auto* engine = mGpu.getEngine())
    {
        engine->setSettings(getEngineSettings());
    }
}

Gpu::Market::Engine::Settings App::getEngineSettings() const
{
    return {
        .widthPx = mLayout.widthPx,
        .heightPx = mLayout.heightPx,
        .pixelRatio = mLayout.pixelRatio,
        .insets = mLayout.insets,
        .grid = mAxes.getGrid(),
        .view = { .time = mView.getTimeRange(), .price = mView.getPriceRange(), .trades = mView.getTrades() },
        .markerSizing = mMarkerSizing,
        .theme = mTheme,
        .highlightedColumn = mPointer.getHighlightedColumn(),
        .highlightedTrade = mPointer.getHighlightedTrade(),
    };
}

void App::update(double nowMs)
{
    mTelemetry.update(nowMs);
    mGpu.restartIfLost(nowMs);
    if (auto* engine = mGpu.getEngine())
    {
        engine->update(nowMs);
    }
}

bool App::needsRender() const
{
    const auto* engine = mGpu.getEngine();
    return engine != nullptr && engine->needsRender();
}

void App::render()
{
    if (auto* engine = mGpu.getEngine())
    {
        engine->render();
        mTelemetry.recordFrame(mSummaries.getNumColumns() > 0, mView.getTrades().size());
    }
}

#ifndef __EMSCRIPTEN__
void App::processEvents()
{
    if (auto* engine = mGpu.getEngine())
    {
        engine->processEvents();
    }
}
#endif

void App::setWakeCallback(std::function<void()> onWake)
{
    mGpu.setWakeCallback(std::move(onWake));
}

void App::readPixels(Gpu::Market::Engine::PixelsCallback onDone)
{
    auto* engine = mGpu.getEngine();
    if (!engine)
    {
        onDone(std::unexpected("The GPU is restarting"));
        return;
    }
    engine->readPixels(std::move(onDone));
}

GpuSession::Health App::getGpuHealth() const
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
        .summariesBytes = mSummaries.getBytes(),
        .gpuBytes = engine ? engine->getGpuBytes() : 0,
    };
}

void App::setProfiling(bool enabled)
{
    mProfiling = enabled;
    syncProfiling();
}

void App::setStatsCallback(Gpu::Market::Engine::StatsCallback onStats)
{
    mOnStats = std::move(onStats);
}

bool App::loseGpu()
{
    auto* engine = mGpu.getEngine();
    if (!engine)
    {
        return false;
    }
    engine->loseDevice();
    return true;
}

void App::setTelemetry(std::unique_ptr<Core::Telemetry::Sink> sink, std::vector<Core::Telemetry::Property> environment)
{
    mTelemetry.setSink(std::move(sink), std::move(environment), mGpu.getInfo());
    syncProfiling();
}

void App::flushTelemetry()
{
    mTelemetry.flush();
}

void App::syncProfiling()
{
    if (mProfiling)
    {
        mGpu.setProfiling(Gpu::Market::Engine::ProfilingOn{});
    }
    else if (mTelemetry.isEnabled())
    {
        mGpu.setProfiling(Gpu::Market::Engine::ProfilingOn{ .gpuTimingProbability = TelemetryGpuTimingProbability });
    }
    else
    {
        mGpu.setProfiling(Gpu::Market::Engine::ProfilingOff{});
    }
}

std::expected<void, std::string> App::setUpEngine(Gpu::Market::Engine& engine)
{
    // Fails only if the new GPU holds fewer than the old one did.
    if (auto set = engine.setTrades(mData.getTrades()); !set)
    {
        return set;
    }
    engine.setColumns(mSummaries.getEngineColumns());
    engine.setSettings(getEngineSettings());
    return {};
}

}  // namespace Viewer
