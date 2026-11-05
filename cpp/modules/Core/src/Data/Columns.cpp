#include "Core/Data/Columns.hpp"

#include <algorithm>
#include <cstdint>

namespace Core::Data {

std::vector<ColumnSummary> summarizeColumns(const PreprocessedTrades& trades, TimeRange view, std::size_t across)
{
    std::vector<ColumnSummary> columns;
    const std::int64_t length = view.getLengthUs();
    if (across == 0 || length <= 0) {
        return columns;
    }
    const auto& timeUs = trades.getTrades().timeUs;
    const auto& instrument = trades.getTrades().instrument;
    const auto begin = timeUs.begin();
    auto first = std::lower_bound(begin, timeUs.end(), view.startUs);
    for (std::size_t i = 0; i < across; ++i) {
        // Exact in 64 bits: a week is 6·10^11 µs, times a few thousand columns.
        const std::int64_t endUs = view.startUs + static_cast<std::int64_t>(i + 1) * length / static_cast<std::int64_t>(across);
        const auto last = std::lower_bound(first, timeUs.end(), endUs);
        const IndexRange indices{ .first = static_cast<std::size_t>(first - begin), .last = static_cast<std::size_t>(last - begin) };
        if (const auto price = trades.getPriceRange(indices)) {
            const std::int64_t buyUnits = trades.getBuyAmountUnits(indices);
            columns.push_back({
                .index = i,
                .trades = indices,
                .price = *price,
                .buyAmount = instrument.toAmount(buyUnits),
                .sellAmount = instrument.toAmount(trades.getAmountUnits(indices) - buyUnits),
                .buyCount = trades.getBuyCount(indices),
            });
        }
        first = last;
    }
    return columns;
}

} // namespace Core::Data
