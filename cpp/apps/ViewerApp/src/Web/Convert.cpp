#include "Web/Convert.hpp"

#include "Core/Memory.hpp"

#include <cstdint>
#include <type_traits>
#include <variant>

namespace Viewer::Web {

namespace {

constexpr double UsPerMs = 1000.0;

/** An array of `items`, each as `map` gives it. */
template<typename Items, typename Map>
val toArray(const Items& items, Map map)
{
    val array = val::array();
    for (const auto& item : items)
    {
        array.call<void>("push", map(item));
    }
    return array;
}

}  // namespace

namespace Convert::ToWeb {

namespace {

/** { red, green, blue }, each from 0 to 1. */
val color(Gpu::Type::Color color)
{
    val object = val::object();
    object.set("red", color.red);
    object.set("green", color.green);
    object.set("blue", color.blue);
    return object;
}

const char* bound(Gpu::Market::Type::LegendSize::Bound bound)
{
    switch (bound)
    {
    case Gpu::Market::Type::LegendSize::Bound::AtMost:
        return "atMost";
    case Gpu::Market::Type::LegendSize::Bound::AtLeast:
        return "atLeast";
    case Gpu::Market::Type::LegendSize::Bound::Exact:
        break;
    }
    return "exact";
}

const char* untimedReason(Gpu::Type::FrameStats::UntimedReason reason)
{
    using Reason = Gpu::Type::FrameStats::UntimedReason;
    switch (reason)
    {
    case Reason::Undefined:
        return "undefined";
    case Reason::ProfilingOff:
        return "profilingOff";
    case Reason::Unsupported:
        return "unsupported";
    case Reason::NoFreeSlot:
        return "noFreeSlot";
    case Reason::NotStarted:
        return "notStarted";
    case Reason::ReadbackFailed:
        return "readbackFailed";
    case Reason::Overflow:
        return "overflow";
    case Reason::NotSampled:
        return "notSampled";
    }
    return "undefined";
}

val cpuTimes(const Gpu::Type::FrameStats::Cpu& cpu)
{
    val object = val::object();
    object.set("acquireMs", cpu.acquireMs);
    object.set("encodeMs", cpu.encodeMs);
    object.set("submitMs", cpu.submitMs);
    return object;
}

val gpuTimes(const Gpu::Type::FrameStats::Gpu& gpu)
{
    val object = val::object();
    object.set("totalMs", gpu.totalMs);
    object.set("passes", toArray(gpu.passes, [](const auto& pass) {
                   val entry = val::object();
                   entry.set("name", pass.name);
                   entry.set("gpuMs", pass.gpuMs);
                   return entry;
               }));
    return object;
}

}  // namespace

TradeStats tradeStats(const Core::Data::TradeStats& stats)
{
    return {
        .count = static_cast<double>(stats.count),
        .firstMs = static_cast<double>(stats.firstUs) / UsPerMs,
        .lastMs = static_cast<double>(stats.lastUs) / UsPerMs,
        .totalAmount = stats.totalAmount,
    };
}

TimeRange timeRange(Core::TimeRange range)
{
    return { .startMs = static_cast<double>(range.startUs) / UsPerMs, .endMs = static_cast<double>(range.endUs) / UsPerMs };
}

val ticks(std::span<const Core::Plot::Axis::Tick> ticks)
{
    return toArray(ticks, [](const Core::Plot::Axis::Tick& tick) {
        val object = val::object();
        object.set("fraction", tick.fraction);
        object.set("label", tick.label);
        return object;
    });
}

val legend(const Gpu::Market::Type::Legend& legend)
{
    val object = val::object();
    object.set("buy", color(legend.buy));
    object.set("sell", color(legend.sell));
    object.set("opacity", legend.opacity);
    object.set("sizes", toArray(legend.sizes, [](const Gpu::Market::Type::LegendSize& size) {
                   val entry = val::object();
                   entry.set("amount", size.amount);
                   entry.set("radiusCssPx", size.radiusCssPx);
                   entry.set("bound", bound(size.bound));
                   return entry;
               }));
    return object;
}

val pickedTrade(const Pointer::Picked::Trade& trade)
{
    val object = val::object();
    // Exact: microseconds since 1970 stay below 2^53 for another 250 years.
    object.set("timeUs", static_cast<double>(trade.timeUs));
    object.set("price", trade.price);
    object.set("amount", trade.amount);
    object.set("side", trade.side == Core::Data::Side::Buy ? "buy" : "sell");
    object.set("centerXPx", trade.centerXPx);
    object.set("centerYPx", trade.centerYPx);
    return object;
}

val pickedColumn(const Pointer::Picked::Column& column)
{
    const auto& summary = column.summary;
    val object = val::object();
    object.set("startUs", static_cast<double>(column.time.startUs));
    object.set("endUs", static_cast<double>(column.time.endUs));
    object.set("count", static_cast<double>(summary.trades.size()));
    object.set("buyCount", static_cast<double>(summary.buyCount));
    object.set("buyAmount", summary.buyAmount);
    object.set("sellAmount", summary.sellAmount);
    object.set("priceLow", summary.price.low);
    object.set("priceHigh", summary.price.high);
    object.set("centerXPx", column.centerXPx);
    object.set("topPx", column.topPx);
    object.set("bottomPx", column.bottomPx);
    return object;
}

val instrument(const Core::Data::Instrument& instrument)
{
    val object = val::object();
    object.set("base", instrument.base);
    object.set("quote", instrument.quote);
    object.set("amountDecimals", instrument.amountDecimals);
    object.set("priceDecimals", instrument.priceDecimals);
    return object;
}

val gpuHealth(const GpuSession::Health& health)
{
    val object = val::object();
    object.set("restarting", health.restarting);
    object.set("restarts", static_cast<double>(health.restarts));
    object.set("failure", health.failure ? val(*health.failure) : val::null());
    object.set("errors", toArray(health.errors, [](const std::string& error) { return val(error); }));
    object.set("errorCount", static_cast<double>(health.errorCount));
    return object;
}

val memoryStats(const App::MemoryStats& memory, std::size_t loadingBytes)
{
    val object = val::object();
    object.set("tradesBytes", static_cast<double>(memory.tradesBytes));
    object.set("totalsBytes", static_cast<double>(memory.totalsBytes));
    object.set("priceTreeBytes", static_cast<double>(memory.priceTreeBytes));
    object.set("summariesBytes", static_cast<double>(memory.summariesBytes));
    object.set("loadingBytes", static_cast<double>(loadingBytes));
    object.set("gpuBytes", static_cast<double>(memory.gpuBytes));
    object.set("heapBytes", static_cast<double>(Core::Memory::getProcessBytes().value_or(0)));
    return object;
}

val scenes(std::span<const Scene> scenes)
{
    return toArray(scenes, [](const Scene& scene) {
        val object = val::object();
        object.set("name", std::string(scene.name));
        object.set("description", std::string(scene.description));
        object.set("widthPx", scene.widthPx);
        object.set("heightPx", scene.heightPx);
        return object;
    });
}

val patches(const std::vector<Scene::Patch>& patches)
{
    return toArray(patches, [](const Scene::Patch& patch) {
        val object = val::object();
        object.set("what", patch.what);
        object.set("xPx", patch.xPx);
        object.set("yPx", patch.yPx);
        object.set("widthPx", patch.widthPx);
        object.set("heightPx", patch.heightPx);
        object.set("rgba", toArray(patch.rgba, [](std::uint8_t channel) { return val(channel); }));
        object.set("tolerance", patch.tolerance);
        return object;
    });
}

val frameStats(const Gpu::Type::FrameStats& stats)
{
    val object = val::object();
    object.set("frame", static_cast<double>(stats.frame));
    object.set("cpu", stats.cpu ? cpuTimes(*stats.cpu) : val::null());
    object.set("gpu", stats.gpu ? gpuTimes(*stats.gpu) : val::null());
    object.set("untimed", stats.gpu ? val::null() : val(untimedReason(stats.gpu.error())));
    object.set("buffers", stats.buffers);
    object.set("textures", stats.textures);
    object.set("gpuBytes", static_cast<double>(stats.gpuBytes));
    return object;
}

val telemetryProperties(const std::vector<Core::Telemetry::Property>& properties)
{
    val object = val::object();
    for (const auto& property : properties)
    {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::int64_t>)
                {
                    object.set(property.name, static_cast<double>(value));
                }
                else
                {
                    object.set(property.name, value);
                }
            },
            property.value);
    }
    return object;
}

val loadResult(const std::optional<std::string>& error, const std::vector<std::string>& warnings)
{
    val object = val::object();
    object.set("error", error ? val(*error) : val::null());
    object.set("warnings", toArray(warnings, [](const std::string& warning) { return val(warning); }));
    return object;
}

}  // namespace Convert::ToWeb

namespace Convert::FromWeb {

namespace {

/** From { red, green, blue }, each from 0 to 1. */
Gpu::Type::Color color(const val& color)
{
    return { .red = color["red"].as<float>(), .green = color["green"].as<float>(), .blue = color["blue"].as<float>() };
}

}  // namespace

Gpu::Type::PowerPreference powerPreference(const std::string& value)
{
    if (value == "low-power")
    {
        return Gpu::Type::PowerPreference::LowPower;
    }
    if (value == "high-performance")
    {
        return Gpu::Type::PowerPreference::HighPerformance;
    }
    return Gpu::Type::PowerPreference::Default;
}

Gpu::Market::Type::MarkerScale markerScale(const std::string& value)
{
    return value == "log" ? Gpu::Market::Type::MarkerScale::Log : Gpu::Market::Type::MarkerScale::Area;
}

Gpu::Market::Type::Theme theme(const val& theme)
{
    return {
        .plot = { .background = color(theme["background"]), .grid = color(theme["grid"]) },
        .buy = color(theme["buy"]),
        .sell = color(theme["sell"]),
        .markerOpacity = theme["markerOpacity"].as<float>(),
        .cursor = color(theme["cursor"]),
    };
}

std::vector<Core::Telemetry::Property> telemetryProperties(const val& object)
{
    std::vector<Core::Telemetry::Property> properties;
    const val keys = val::global("Object").call<val>("keys", object);
    for (std::size_t i = 0; i < keys["length"].as<std::size_t>(); ++i)
    {
        const auto name = keys[i].as<std::string>();
        const val value = object[name];
        const auto type = value.typeOf().as<std::string>();
        if (type == "number")
        {
            properties.push_back({ .name = name, .value = value.as<double>() });
        }
        else if (type == "string")
        {
            properties.push_back({ .name = name, .value = value.as<std::string>() });
        }
        else if (type == "boolean")
        {
            properties.push_back({ .name = name, .value = value.as<bool>() });
        }
    }
    return properties;
}

}  // namespace Convert::FromWeb

}  // namespace Viewer::Web
