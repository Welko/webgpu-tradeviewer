#pragma once

#include "Core/Data/TimeRange.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
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

/** Positions [first, last) in a Trades. */
struct IndexRange
{
    std::size_t first;
    std::size_t last;

    [[nodiscard]] std::size_t size() const { return last - first; }
    [[nodiscard]] bool empty() const { return first == last; }

    bool operator==(const IndexRange&) const = default;
};

/** Where the trades in `range` are, start inclusive and end exclusive. */
inline IndexRange tradesIn(const Trades& trades, TimeRange range)
{
    const auto begin = trades.timeUs.begin();
    const auto first = std::lower_bound(begin, trades.timeUs.end(), range.startUs);
    const auto last = std::lower_bound(first, trades.timeUs.end(), range.endUs);
    return { .first = static_cast<std::size_t>(first - begin), .last = static_cast<std::size_t>(last - begin) };
}

struct PriceRange
{
    double low;
    double high;
};

/** The lowest and highest price of the trades in `indices`, or nothing if there are none. Reads every one. */
inline std::optional<PriceRange> priceRange(const Trades& trades, IndexRange indices)
{
    if (indices.empty()) {
        return std::nullopt;
    }
    const auto begin = trades.price.begin();
    const auto [low, high] = std::minmax_element(begin + static_cast<std::ptrdiff_t>(indices.first),
        begin + static_cast<std::ptrdiff_t>(indices.last));
    return PriceRange{ .low = *low, .high = *high };
}

} // namespace Core::Data
