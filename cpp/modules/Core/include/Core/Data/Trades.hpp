#pragma once

#include "Core/Range.hpp"

#include <algorithm>
#include <cmath>
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

/**
 * Amounts are summed in whole units of 1e-8, the finest step Binance trades
 * in, so totals are exact and the same however they're added up.
 */
constexpr double AmountUnit = 1e-8;

namespace detail {

inline std::int64_t toAmountUnits(double amount)
{
    return std::llround(amount / AmountUnit);
}

} // namespace detail

/** How many trades there are, when, and how much they traded. */
struct TradeStats
{
    std::size_t count;
    std::int64_t firstUs;  // 0 without trades
    std::int64_t lastUs;
    double totalAmount;
};

inline TradeStats getStats(const Trades& trades)
{
    if (trades.empty()) {
        return {};
    }
    std::int64_t totalUnits = 0;
    for (const double amount : trades.amount) {
        totalUnits += detail::toAmountUnits(amount);
    }
    return {
        .count = trades.size(),
        .firstUs = trades.timeUs.front(),
        .lastUs = trades.timeUs.back(),
        .totalAmount = static_cast<double>(totalUnits) * AmountUnit,
    };
}

/** Where the trades in `range` are, start inclusive and end exclusive. */
inline IndexRange getTradesIn(const Trades& trades, TimeRange range)
{
    const auto begin = trades.timeUs.begin();
    const auto first = std::lower_bound(begin, trades.timeUs.end(), range.startUs);
    const auto last = std::lower_bound(first, trades.timeUs.end(), range.endUs);
    return { .first = static_cast<std::size_t>(first - begin), .last = static_cast<std::size_t>(last - begin) };
}

/**
 * The total amount of any range of trades in constant time, in all and of
 * buys alone, and how many were buys: running totals, built once.
 */
class RunningTotals
{
public:
    RunningTotals() = default;

    explicit RunningTotals(const Trades& trades)
    {
        mRunning.reserve(trades.size() + 1);
        mRunningBuys.reserve(trades.size() + 1);
        mBuyCounts.reserve(trades.size() + 1);
        for (std::size_t i = 0; i < trades.size(); ++i) {
            const std::int64_t units = detail::toAmountUnits(trades.amount[i]);
            const bool buy = trades.side[i] == Side::Buy;
            mRunning.push_back(mRunning.back() + units);
            mRunningBuys.push_back(mRunningBuys.back() + (buy ? units : 0));
            mBuyCounts.push_back(mBuyCounts.back() + (buy ? 1 : 0));
        }
    }

    /** In units of AmountUnit. */
    [[nodiscard]] std::int64_t getUnits(IndexRange indices) const { return mRunning[indices.last] - mRunning[indices.first]; }

    [[nodiscard]] double getAmount(IndexRange indices) const { return static_cast<double>(getUnits(indices)) * AmountUnit; }

    /** Of the buys alone, in units of AmountUnit. */
    [[nodiscard]] std::int64_t getBuyUnits(IndexRange indices) const
    {
        return mRunningBuys[indices.last] - mRunningBuys[indices.first];
    }

    [[nodiscard]] std::size_t getBuyCount(IndexRange indices) const { return mBuyCounts[indices.last] - mBuyCounts[indices.first]; }

private:
    // Element i covers the first i trades.
    std::vector<std::int64_t> mRunning{ 0 };
    std::vector<std::int64_t> mRunningBuys{ 0 };
    std::vector<std::uint32_t> mBuyCounts{ 0 };
};

} // namespace Core::Data
