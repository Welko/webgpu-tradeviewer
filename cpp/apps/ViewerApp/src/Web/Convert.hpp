#pragma once

#include "Viewer/App.hpp"
#include "Viewer/GpuSession.hpp"
#include "Viewer/Pointer.hpp"
#include "Viewer/Scene.hpp"

#include "Core/Data/Instrument.hpp"
#include "Core/Data/Trades.hpp"
#include "Core/Plot/Axis.hpp"
#include "Core/Range.hpp"
#include "Core/Telemetry.hpp"
#include "Gpu/Market/Type.hpp"
#include "Gpu/Type.hpp"

#include <emscripten/val.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

/**
 * Values between C++ and JavaScript, ToWeb and FromWeb. Numbers JavaScript
 * can't hold exactly, such as 64-bit counts, become doubles; times become
 * milliseconds for Date, unless named in microseconds.
 */
namespace Viewer::Web {

using emscripten::val;

/** Core::Data::TradeStats in numbers JavaScript reads exactly, with times in milliseconds. */
struct TradeStats
{
    double count;
    double firstMs;
    double lastMs;
    double totalAmount;
};

/** A time range in milliseconds since the epoch. */
struct TimeRange
{
    double startMs;
    double endMs;
};

namespace Convert::ToWeb {

[[nodiscard]] TradeStats tradeStats(const Core::Data::TradeStats& stats);
[[nodiscard]] TimeRange timeRange(Core::TimeRange range);

/** An array of { fraction, label }. */
[[nodiscard]] val ticks(std::span<const Core::Plot::Axis::Tick> ticks);

/** { buy, sell, opacity, sizes: [{ amount, radiusCssPx, bound }] }, `bound` being "exact", "atMost" or "atLeast". */
[[nodiscard]] val legend(const Gpu::Market::Type::Legend& legend);

/** { timeUs, price, amount, side, centerXPx, centerYPx }, `side` being "buy" or "sell". */
[[nodiscard]] val pickedTrade(const Pointer::Picked::Trade& trade);

/** { startUs, endUs, count, buyCount, buyAmount, sellAmount, priceLow, priceHigh, centerXPx, topPx, bottomPx }. */
[[nodiscard]] val pickedColumn(const Pointer::Picked::Column& column);

/** { base, quote, amountDecimals, priceDecimals }. */
[[nodiscard]] val instrument(const Core::Data::Instrument& instrument);

/** { restarting, restarts, failure, errors, errorCount }, `failure` a message or null. */
[[nodiscard]] val gpuHealth(const GpuSession::Health& health);

/**
 * { tradesBytes, totalsBytes, priceTreeBytes, summariesBytes, loadingBytes,
 * gpuBytes, heapBytes }: `memory`, with a file being read and the whole heap.
 */
[[nodiscard]] val memoryStats(const App::MemoryStats& memory, std::size_t loadingBytes);

/** An array of { name, description, widthPx, heightPx }. */
[[nodiscard]] val scenes(std::span<const Scene> scenes);

/** An array of { what, xPx, yPx, widthPx, heightPx, rgba, tolerance }. */
[[nodiscard]] val patches(const std::vector<Scene::Patch>& patches);

/**
 * { frame, cpu, gpu, untimed, buffers, textures, gpuBytes }: `cpu` is
 * { acquireMs, encodeMs, submitMs } or null; `gpu` is { totalMs, passes:
 * [{ name, gpuMs }] }, or null with `untimed` saying why.
 */
[[nodiscard]] val frameStats(const Gpu::Type::FrameStats& stats);

/** An object of telemetry's properties. */
[[nodiscard]] val telemetryProperties(const std::vector<Core::Telemetry::Property>& properties);

/** How loading went, as { error, warnings }: a message or null, and an array of messages. */
[[nodiscard]] val loadResult(const std::optional<std::string>& error, const std::vector<std::string>& warnings = {});

}  // namespace Convert::ToWeb

namespace Convert::FromWeb {

/** The WebGPU spelling: "low-power", "high-performance", anything else the default. */
[[nodiscard]] Gpu::Type::PowerPreference powerPreference(const std::string& value);

/** "area" or "log". */
[[nodiscard]] Gpu::Market::Type::MarkerScale markerScale(const std::string& value);

/** From { background, grid, buy, sell, cursor, markerOpacity }, each color a { red, green, blue } from 0 to 1. */
[[nodiscard]] Gpu::Market::Type::Theme theme(const val& theme);

/** An object's numbers, strings and booleans, as telemetry's properties. Anything else is left out. */
[[nodiscard]] std::vector<Core::Telemetry::Property> telemetryProperties(const val& object);

}  // namespace Convert::FromWeb

}  // namespace Viewer::Web
