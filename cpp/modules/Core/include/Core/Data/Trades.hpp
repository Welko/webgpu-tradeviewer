#pragma once

#include "Core/Data/Instrument.hpp"
#include "Core/Range.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace Core::Data {

/** The side that took liquidity: a buy lifted an ask, a sell hit a bid. */
enum class Side : std::uint8_t
{
    Buy,
    Sell,
};

/** Trades in time order, one vector per column, and what they're in. */
struct Trades
{
    std::vector<std::int64_t> timeUs;  // exchange time, microseconds since the Unix epoch (UTC)
    std::vector<double> price;  // in the quote asset
    std::vector<double> amount;  // in the base asset
    std::vector<Side> side;
    Instrument instrument{};

    [[nodiscard]] std::size_t size() const { return timeUs.size(); }
    [[nodiscard]] bool empty() const { return timeUs.empty(); }

    /** The memory held, room to grow included: 25 bytes per trade, once shrunk. */
    [[nodiscard]] std::size_t getBytes() const
    {
        return timeUs.capacity() * sizeof(std::int64_t) + price.capacity() * sizeof(double) + amount.capacity() * sizeof(double)
               + side.capacity() * sizeof(Side);
    }

    /**
     * Gives back the room to grow that reading left, up to half of it.
     * Copies rather than calling shrink_to_fit(), which libstdc++ skips
     * without exceptions.
     */
    void shrinkToFit()
    {
        const auto shrink = [](auto& column) {
            if (column.capacity() > column.size()) {
                column = std::remove_reference_t<decltype(column)>(column.begin(), column.end());
            }
        };
        shrink(timeUs);
        shrink(price);
        shrink(amount);
        shrink(side);
    }
};

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
    // In whole units of the finest step, so the total is exact.
    std::int64_t totalUnits = 0;
    for (const double amount : trades.amount) {
        totalUnits += trades.instrument.toAmountUnits(amount);
    }
    return {
        .count = trades.size(),
        .firstUs = trades.timeUs.front(),
        .lastUs = trades.timeUs.back(),
        .totalAmount = trades.instrument.toAmount(totalUnits),
    };
}

/**
 * The amounts `lowShare` and `highShare` of the way up the trades sorted by
 * amount, each share from 0 to 1 (0.5 is the median), by the nearest-rank
 * method. Zero without trades. Sorts a copy of the amounts, so it's for now
 * and then, not for every frame.
 */
inline ValueRange getAmountPercentiles(const Trades& trades, double lowShare, double highShare)
{
    if (trades.empty()) {
        return {};
    }
    std::vector<double> amounts = trades.amount;
    const auto at = [&](double share) {
        const auto rank = static_cast<std::size_t>(std::ceil(share * static_cast<double>(amounts.size())));
        const auto nth = amounts.begin() + static_cast<std::ptrdiff_t>(std::clamp<std::size_t>(rank, 1, amounts.size()) - 1);
        std::ranges::nth_element(amounts, nth);
        return *nth;
    };
    return { .low = at(lowShare), .high = at(highShare) };
}

/** Where the trades in `range` are, start inclusive and end exclusive. */
inline IndexRange getTradesIn(const Trades& trades, TimeRange range)
{
    const auto begin = trades.timeUs.begin();
    const auto first = std::lower_bound(begin, trades.timeUs.end(), range.startUs);
    const auto last = std::lower_bound(first, trades.timeUs.end(), range.endUs);
    return { .first = static_cast<std::size_t>(first - begin), .last = static_cast<std::size_t>(last - begin) };
}

} // namespace Core::Data
