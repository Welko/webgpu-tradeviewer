#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Core::Data {

/** The side that took liquidity: a buy lifted an ask, a sell hit a bid. */
enum class Side : std::uint8_t
{
    Buy,
    Sell,
};

/** Trades in time order, one vector per column. */
struct Trades
{
    std::vector<std::int64_t> timeUs;  // exchange time, microseconds since the Unix epoch (UTC)
    std::vector<double> price;
    std::vector<double> amount;  // in the base asset (BTC)
    std::vector<Side> side;

    [[nodiscard]] std::size_t size() const { return timeUs.size(); }
    [[nodiscard]] bool empty() const { return timeUs.empty(); }
};

/** How many trades there are, when, and how much they traded. */
struct TradeSummary
{
    std::size_t count;
    std::int64_t firstUs;  // 0 without trades
    std::int64_t lastUs;
    double totalAmount;
};

inline TradeSummary summarize(const Trades& trades)
{
    if (trades.empty()) {
        return {};
    }
    double totalAmount = 0.0;
    for (const double amount : trades.amount) {
        totalAmount += amount;
    }
    return {
        .count = trades.size(),
        .firstUs = trades.timeUs.front(),
        .lastUs = trades.timeUs.back(),
        .totalAmount = totalAmount,
    };
}

} // namespace Core::Data
