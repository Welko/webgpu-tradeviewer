#include "Core/Data/PriceTree.hpp"

#include <algorithm>

namespace Core::Data {

namespace {

ValueRange join(ValueRange a, ValueRange b)
{
    return { .low = std::min(a.low, b.low), .high = std::max(a.high, b.high) };
}

} // namespace

PriceTree::PriceTree(const Trades& trades)
    : mSize(trades.size()),
      mNodes(2 * trades.size())
{
    for (std::size_t i = 0; i < mSize; ++i) {
        mNodes[mSize + i] = { .low = trades.price[i], .high = trades.price[i] };
    }
    for (std::size_t k = mSize - 1; k > 0 && k < mSize; --k) {
        mNodes[k] = join(mNodes[2 * k], mNodes[2 * k + 1]);
    }
}

std::optional<ValueRange> PriceTree::getRange(IndexRange indices) const
{
    std::size_t left = std::min(indices.first, mSize) + mSize;
    std::size_t right = std::min(indices.last, mSize) + mSize;
    std::optional<ValueRange> range;
    const auto add = [&range](ValueRange node) { range = range ? join(*range, node) : node; };
    // Climbs from both ends, taking each node that lies wholly inside.
    while (left < right) {
        if (left % 2 == 1) {
            add(mNodes[left++]);
        }
        if (right % 2 == 1) {
            add(mNodes[--right]);
        }
        left /= 2;
        right /= 2;
    }
    return range;
}

} // namespace Core::Data
