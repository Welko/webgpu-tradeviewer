#pragma once

#include "Core/Data/TimeRange.hpp"

#include <algorithm>
#include <cmath>
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

/**
 * Amounts are summed in whole units of 1e-8, the finest step Binance trades
 * in, so totals are exact and the same however they're added up.
 */
constexpr double AmountUnit = 1e-8;

inline std::int64_t toAmountUnits(double amount)
{
    return std::llround(amount / AmountUnit);
}

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
    std::int64_t totalUnits = 0;
    for (const double amount : trades.amount) {
        totalUnits += toAmountUnits(amount);
    }
    return {
        .count = trades.size(),
        .firstUs = trades.timeUs.front(),
        .lastUs = trades.timeUs.back(),
        .totalAmount = static_cast<double>(totalUnits) * AmountUnit,
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

/** The total amount of any range of trades in constant time: running totals, built once. */
class AmountTotals
{
public:
    AmountTotals() = default;

    explicit AmountTotals(const Trades& trades)
    {
        mRunning.reserve(trades.size() + 1);
        for (const double amount : trades.amount) {
            mRunning.push_back(mRunning.back() + toAmountUnits(amount));
        }
    }

    /** In units of AmountUnit. */
    [[nodiscard]] std::int64_t unitsIn(IndexRange indices) const { return mRunning[indices.last] - mRunning[indices.first]; }

    [[nodiscard]] double in(IndexRange indices) const { return static_cast<double>(unitsIn(indices)) * AmountUnit; }

private:
    std::vector<std::int64_t> mRunning{ 0 };  // mRunning[i]: the first i trades' total
};

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
