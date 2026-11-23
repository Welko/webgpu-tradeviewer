#include "Web/Bridge.hpp"

#include "Viewer/Scene.hpp"

#include <expected>
#include <string_view>
#include <utility>
#include <variant>

namespace Viewer::Web {

namespace {

namespace ToWeb = Convert::ToWeb;
namespace FromWeb = Convert::FromWeb;

/** Hands telemetry's events to JavaScript, which sends them on: `onEvent(name, properties)`. */
class TelemetrySink final : public Core::Telemetry::Sink
{
public:
    explicit TelemetrySink(val onEvent)
        : mOnEvent(std::move(onEvent))
    {}

    void send(const Core::Telemetry::Event& event) override { mOnEvent(event.name, ToWeb::telemetryProperties(event.properties)); }

private:
    val mOnEvent;
};

}  // namespace

void Bridge::init(std::string selector, const std::string& powerPreference, val onDone)
{
    const std::weak_ptr<std::unique_ptr<App>> slot = mApp;
    const Gpu::Market::Engine::Params params{
        .target = Gpu::Type::Target::Canvas{ .selector = std::move(selector) },
        .powerPreference = FromWeb::powerPreference(powerPreference),
    };
    App::create(params, [slot, onDone](App::Result result) {
        const auto app = slot.lock();
        if (!app)
        {
            // delete() was called before initialization finished.
            return;
        }
        if (!result)
        {
            onDone(result.error());
            return;
        }
        *app = std::move(*result);
        if (auto set = (*app)->setTrades(generateSyntheticDay()); !set)
        {
            onDone(set.error());
            return;
        }
        onDone(val::null());
    });
}

Gpu::Type::Info Bridge::getInfo() const
{
    return withApp([](const App& app) { return app.getInfo(); }, Gpu::Type::Info{});
}

// Frames

void Bridge::resize(std::uint32_t widthPx, std::uint32_t heightPx)
{
    withApp([&](App& app) { app.setSize(widthPx, heightPx); });
}

void Bridge::update(double nowMs)
{
    withApp([&](App& app) { app.update(nowMs); });
}

bool Bridge::needsRender() const
{
    return withApp([](const App& app) { return app.needsRender(); }, false);
}

void Bridge::render()
{
    withApp([](App& app) { app.render(); });
}

void Bridge::setWakeCallback(val onWake)
{
    withApp([&](App& app) { app.setWakeCallback([onWake] { onWake(); }); });
}

// What's shown

TimeRange Bridge::getView() const
{
    return withApp([](const App& app) { return ToWeb::timeRange(app.getView().getTimeRange()); }, TimeRange{});
}

TradeStats Bridge::getViewStats() const
{
    return withApp([](const App& app) { return ToWeb::tradeStats(app.getView().getStats()); }, TradeStats{});
}

val Bridge::getAxes() const
{
    return withApp(
        [](const App& app) {
            val axes = val::object();
            axes.set("times", ToWeb::ticks(app.getAxes().getTimes()));
            axes.set("prices", ToWeb::ticks(app.getAxes().getPrices()));
            return axes;
        },
        val::null());
}

val Bridge::getLegend() const
{
    return withApp([](const App& app) { return ToWeb::legend(app.getLegend()); }, val::null());
}

val Bridge::getSummaries() const
{
    return withApp(
        [](const App& app) {
            const auto& summaries = app.getSummaries();
            if (summaries.getNumColumns() == 0)
            {
                return val::null();
            }
            val result = val::object();
            result.set("across", summaries.getNumColumns());
            result.set("withTrades", static_cast<double>(summaries.getColumns().size()));
            return result;
        },
        val::null());
}

TradeStats Bridge::getTradeStats() const
{
    return withApp([](const App& app) { return ToWeb::tradeStats(Core::Data::getStats(app.getTrades())); }, TradeStats{});
}

val Bridge::getInstrument() const
{
    return withApp([](const App& app) { return ToWeb::instrument(app.getInstrument()); }, val::null());
}

// Changing it

void Bridge::setTheme(const val& theme)
{
    withApp([&](App& app) { app.setTheme(FromWeb::theme(theme)); });
}

void Bridge::setPlotInsets(std::uint32_t leftPx, std::uint32_t topPx, std::uint32_t rightPx, std::uint32_t bottomPx)
{
    withApp([&](App& app) { app.setPlotInsets({ .left = leftPx, .top = topPx, .right = rightPx, .bottom = bottomPx }); });
}

void Bridge::setPixelRatio(float ratio)
{
    withApp([&](App& app) { app.setPixelRatio(ratio); });
}

void Bridge::setMarkerScale(const std::string& scale)
{
    withApp([&](App& app) { app.setMarkerScale(FromWeb::markerScale(scale)); });
}

void Bridge::setSummariesAllowed(bool allowed)
{
    withApp([&](App& app) { app.setSummariesAllowed(allowed); });
}

void Bridge::setSummaryThreshold(double tradesPerColumn)
{
    withApp([&](App& app) { app.setSummaryThreshold(tradesPerColumn); });
}

void Bridge::resetView()
{
    withApp([](App& app) {
        if (const auto initialView = app.getInitialView())
        {
            app.setView(*initialView);
        }
    });
}

void Bridge::showDay()
{
    withApp([](App& app) {
        if (!app.getTrades().empty())
        {
            app.setView(Core::getUtcDay(app.getTrades().timeUs.front()));
        }
    });
}

void Bridge::zoomAt(double xPx, double factor)
{
    withApp([&](App& app) { app.zoomAt(xPx, factor); });
}

void Bridge::panBy(double dxPx)
{
    withApp([&](App& app) { app.panBy(dxPx); });
}

// The pointer

void Bridge::setPointer(double xPx, double yPx)
{
    withApp([&](App& app) { app.setPointer(xPx, yPx); });
}

void Bridge::clearPointer()
{
    withApp([](App& app) { app.clearPointer(); });
}

val Bridge::getPickedTrade() const
{
    return withApp(
        [](const App& app) {
            const auto* trade = std::get_if<Pointer::Picked::Trade>(&app.getPointer().getPicked());
            return trade ? ToWeb::pickedTrade(*trade) : val::null();
        },
        val::null());
}

val Bridge::getPickedColumn() const
{
    return withApp(
        [](const App& app) {
            const auto* column = std::get_if<Pointer::Picked::Column>(&app.getPointer().getPicked());
            return column ? ToWeb::pickedColumn(*column) : val::null();
        },
        val::null());
}

// Loading a file

void Bridge::beginLoad(const std::string& fileName)
{
    mParser.emplace(fileName);
}

val Bridge::appendChunk(const val& bytes)
{
    if (!mParser)
    {
        return ToWeb::loadResult("beginLoad() wasn't called");
    }
    mChunk.resize(bytes["length"].as<std::size_t>());
    val(emscripten::typed_memory_view(mChunk.size(), mChunk.data())).call<void>("set", bytes);
    if (auto appended = mParser->append(std::string_view(mChunk.data(), mChunk.size())); !appended)
    {
        mParser.reset();
        return ToWeb::loadResult(appended.error());
    }
    return ToWeb::loadResult(std::nullopt);
}

val Bridge::finishLoad()
{
    if (!mParser || !*mApp)
    {
        return ToWeb::loadResult("beginLoad() wasn't called");
    }
    auto parsed = mParser->finish();
    mParser.reset();
    if (!parsed)
    {
        return ToWeb::loadResult(parsed.error());
    }
    if (auto set = (*mApp)->setTrades(std::move(parsed->trades)); !set)
    {
        return ToWeb::loadResult(set.error());
    }
    return ToWeb::loadResult(std::nullopt, parsed->warnings);
}

// Diagnostics

val Bridge::getGpuHealth() const
{
    return withApp([](const App& app) { return ToWeb::gpuHealth(app.getGpuHealth()); }, val::null());
}

val Bridge::getMemoryStats() const
{
    return withApp([this](const App& app) { return ToWeb::memoryStats(app.getMemoryStats(), mParser ? mParser->getBytes() : 0); },
                   val::null());
}

void Bridge::setProfiling(bool enabled)
{
    withApp([&](App& app) { app.setProfiling(enabled); });
}

void Bridge::setStatsCallback(val onStats)
{
    withApp([&](App& app) {
        if (onStats.isNull())
        {
            app.setStatsCallback(nullptr);
            return;
        }
        app.setStatsCallback([onStats](const Gpu::Type::FrameStats& stats) { onStats(ToWeb::frameStats(stats)); });
    });
}

// Telemetry

void Bridge::setTelemetry(val onEvent, const val& environment)
{
    withApp([&](App& app) {
        if (onEvent.isNull() || onEvent.isUndefined())
        {
            app.setTelemetry(nullptr);
            return;
        }
        app.setTelemetry(std::make_unique<TelemetrySink>(std::move(onEvent)), FromWeb::telemetryProperties(environment));
    });
}

void Bridge::flushTelemetry()
{
    withApp([](App& app) { app.flushTelemetry(); });
}

val Bridge::getTelemetryProperties() const
{
    return withApp([](const App& app) { return ToWeb::telemetryProperties(app.getTelemetry().getCommonProperties()); }, val::null());
}

// Image tests

val Bridge::showScene(const std::string& name)
{
    const auto* scene = Scene::find(name);
    if (scene == nullptr)
    {
        return val::null();
    }
    return withApp([scene](App& app) { return ToWeb::patches(Scene::show(*scene, app)); }, val::null());
}

void Bridge::readPixels(val onDone)
{
    if (!*mApp)
    {
        onDone(val::null(), std::string("The viewer isn't ready"));
        return;
    }
    (*mApp)->readPixels([onDone](const std::expected<std::vector<std::uint8_t>, std::string>& pixels) {
        if (!pixels)
        {
            onDone(val::null(), pixels.error());
            return;
        }
        // A copy that outlives the vector.
        onDone(val(emscripten::typed_memory_view(pixels->size(), pixels->data())).call<val>("slice"), val::null());
    });
}

}  // namespace Viewer::Web
