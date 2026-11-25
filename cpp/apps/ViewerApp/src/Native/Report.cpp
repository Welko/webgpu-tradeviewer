#include "Native/Report.hpp"

#include "Core/Log.hpp"
#include "Core/Memory.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <string_view>
#include <variant>

namespace Viewer::Native {

namespace {

constexpr double BytesPerMiB = 1024.0 * 1024.0;

/** "<= " or ">= " before a legend size that stands for the smaller or larger amounts too. */
const char* getBoundPrefix(Gpu::Market::Type::LegendSize::Bound bound)
{
    switch (bound)
    {
    case Gpu::Market::Type::LegendSize::Bound::AtMost:
        return "<= ";
    case Gpu::Market::Type::LegendSize::Bound::AtLeast:
        return ">= ";
    case Gpu::Market::Type::LegendSize::Bound::Exact:
        break;
    }
    return "";
}

}  // namespace

std::string formatUtc(std::int64_t timeUs)
{
    return std::format("{:%F %T}", std::chrono::sys_time<std::chrono::milliseconds>(std::chrono::milliseconds(timeUs / 1000)));
}

std::string getWindowTitle(const App& app)
{
    const auto& instrument = app.getInstrument();
    // In the instrument's own decimals.
    const auto amount = [&](double value) { return std::format("{:.{}f} {}", value, instrument.amountDecimals, instrument.base); };
    const auto price = [&](double value) { return std::format("{:.{}f}", value, instrument.priceDecimals); };
    const auto& picked = app.getPointer().getPicked();
    if (const auto* column = std::get_if<Pointer::Picked::Column>(&picked))
    {
        const auto& summary = column->summary;
        return std::format("Trade Viewer · {} to {} UTC · {} trades ({} buys), {} to {} {}, {} bought, {} sold",
                           formatUtc(column->time.startUs), formatUtc(column->time.endUs), summary.trades.size(), summary.buyCount,
                           price(summary.price.low), price(summary.price.high), instrument.quote, amount(summary.buyAmount),
                           amount(summary.sellAmount));
    }
    if (const auto* trade = std::get_if<Pointer::Picked::Trade>(&picked))
    {
        const auto ms = trade->timeUs / 1000 - (trade->timeUs % 1000 < 0 ? 1 : 0);
        return std::format("Trade Viewer · {} {} at {} {} · {}{:03} UTC", trade->side == Core::Data::Side::Buy ? "Buy" : "Sell",
                           amount(trade->amount), price(trade->price), instrument.quote, formatUtc(ms * 1000), trade->timeUs - ms * 1000);
    }
    const auto view = app.getView().getTimeRange();
    const auto stats = app.getView().getStats();
    return std::format("Trade Viewer · {} to {} UTC · {} trades, {}{}", formatUtc(view.startUs), formatUtc(view.endUs), stats.count,
                       amount(stats.totalAmount), app.getSummaries().getColumnCount() > 0 ? " · summarized: one bar per pixel column" : "");
}

void logTrades(const App& app, bool loaded)
{
    const auto stats = Core::Data::getStats(app.getTrades());
    Core::Log::info("{} {} trades from {} to {} UTC, {:.6f} in total", stats.count, loaded ? "loaded" : "synthetic",
                    formatUtc(stats.firstUs), formatUtc(stats.lastUs), stats.totalAmount);
}

void logLegend(const App& app)
{
    std::string sizes;
    for (const auto& size : app.getLegend().sizes)
    {
        sizes += std::format("{}{}{:.4g} {} {:.3g} px", sizes.empty() ? "" : ", ", getBoundPrefix(size.bound), size.amount,
                             app.getInstrument().base, size.radiusCssPx);
    }
    const bool byArea = app.getMarkerSizing().scale == Core::Plot::MarkerScale::Area;
    Core::Log::info("Buys green, sells red; marker radius by {}: {}", byArea ? "area" : "log", sizes);
}

void logMemory(const App& app)
{
    const auto memory = app.getMemoryStats();
    const auto count = static_cast<double>(std::max<std::size_t>(1, app.getTrades().size()));
    const auto line = [count](std::string_view what, std::uint64_t bytes) {
        return std::format("\n  {:<16} {:>8.1f} MiB  {:>5.1f} B per trade", what, static_cast<double>(bytes) / BytesPerMiB,
                           static_cast<double>(bytes) / count);
    };
    const std::size_t derived = memory.totalsBytes + memory.priceTreeBytes + memory.summariesBytes;
    const double derivedPercent =
        memory.tradesBytes > 0 ? 100.0 * static_cast<double>(derived) / static_cast<double>(memory.tradesBytes) : 0.0;
    const auto process = Core::Memory::getProcessBytes();
    Core::Log::info("Memory:{}{}{}{}{}{}\n  The derived data adds {:.0f}% to the trades.{}", line("trades", memory.tradesBytes),
                    line("running totals", memory.totalsBytes), line("price tree", memory.priceTreeBytes),
                    line("column summaries", memory.summariesBytes), line("total", memory.getCpuBytes()),
                    line("on the GPU", memory.gpuBytes), derivedPercent,
                    process ? std::format(" The process uses {:.1f} MiB in all.", static_cast<double>(*process) / BytesPerMiB) : "");
}

void logControls()
{
    Core::Log::info("Scroll to zoom, drag to pan, point at a trade for its details (in the title); "
                    "R: 12:00-13:00, D: whole day, M: marker size by area or log, S: summaries on or off");
}

}  // namespace Viewer::Native
