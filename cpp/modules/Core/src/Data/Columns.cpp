#include "Core/Data/Columns.hpp"

#include <algorithm>
#include <cstdint>

namespace Core::Data {

std::vector<ColumnSummary> summarizeColumns(const Trades& trades, const RunningTotals& totals, const PriceTree& prices,
                                            TimeRange view, std::size_t across)
{
    std::vector<ColumnSummary> columns;
    const std::int64_t length = view.getLengthUs();
    if (across == 0 || length <= 0) {
        return columns;
    }
    const auto begin = trades.timeUs.begin();
    auto first = std::lower_bound(begin, trades.timeUs.end(), view.startUs);
    for (std::size_t i = 0; i < across; ++i) {
        // Exact in 64 bits: a week is 6·10^11 µs, times a few thousand columns.
        const std::int64_t endUs = view.startUs + static_cast<std::int64_t>(i + 1) * length / static_cast<std::int64_t>(across);
        const auto last = std::lower_bound(first, trades.timeUs.end(), endUs);
        const IndexRange indices{ .first = static_cast<std::size_t>(first - begin), .last = static_cast<std::size_t>(last - begin) };
        if (const auto price = prices.getRange(indices)) {
            const std::int64_t buyUnits = totals.getBuyUnits(indices);
            columns.push_back({
                .index = i,
                .trades = indices,
                .price = *price,
                .buyAmount = static_cast<double>(buyUnits) * AmountUnit,
                .sellAmount = static_cast<double>(totals.getUnits(indices) - buyUnits) * AmountUnit,
                .buyCount = totals.getBuyCount(indices),
            });
        }
        first = last;
    }
    return columns;
}

} // namespace Core::Data
