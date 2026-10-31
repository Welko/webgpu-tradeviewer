#pragma once

#include "Core/Data/Trades.hpp"
#include "Core/Range.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace Core::Data {

/**
 * The lowest and highest price of any range of trades, reading O(log n)
 * entries instead of every trade: a segment tree, built once. It takes 32
 * bytes per trade.
 */
class PriceTree
{
public:
    PriceTree() = default;

    explicit PriceTree(const Trades& trades);

    /** Nothing if `indices` is empty. */
    [[nodiscard]] std::optional<ValueRange> getRange(IndexRange indices) const;

private:
    std::size_t mSize = 0;
    // mNodes[mSize + i] is trade i's price; mNodes[k], for 0 < k < mSize,
    // spans mNodes[2k] and mNodes[2k + 1].
    std::vector<ValueRange> mNodes;
};

} // namespace Core::Data
