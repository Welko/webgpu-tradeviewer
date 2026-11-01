#pragma once

#include "Core/Data/PreprocessedTrades.hpp"
#include "Core/Range.hpp"

#include <cstddef>
#include <vector>

namespace Core::Data {

/** The trades in one column of the plot, summed up. */
struct ColumnSummary
{
    std::size_t index;  // of the columns across, from the left, 0 first
    IndexRange trades;
    ValueRange price;
    double buyAmount;  // BTC
    double sellAmount;
    std::size_t buyCount;  // the rest are sells
};

/**
 * The trades in `view`, split by time into `across` columns of equal length,
 * each summed up: price range, and amount and count by side. Column i holds
 * the trades in [start + i·length / across, start + (i + 1)·length / across),
 * whole microseconds, so each trade is in exactly one.
 *
 * Only columns with trades are returned, so there can be fewer than `across`,
 * with gaps in their indices where nothing traded. Reads O(across · log n)
 * entries, and at most 30 prices per column, however many trades the
 * columns hold.
 */
[[nodiscard]] std::vector<ColumnSummary> summarizeColumns(const PreprocessedTrades& trades, TimeRange view, std::size_t across);

} // namespace Core::Data
