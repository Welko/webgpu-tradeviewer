#include "Core/Data/PreprocessedTrades.hpp"

#include <algorithm>
#include <utility>

namespace Core::Data {

namespace {

ValueRange join(ValueRange a, ValueRange b)
{
    return { .low = std::min(a.low, b.low), .high = std::max(a.high, b.high) };
}

/** The prices of trades `first` to `last` (exclusive), read one by one; `first < last`. */
ValueRange scan(const std::vector<double>& prices, std::size_t first, std::size_t last)
{
    ValueRange range{ .low = prices[first], .high = prices[first] };
    for (std::size_t i = first + 1; i < last; ++i) {
        range = join(range, { .low = prices[i], .high = prices[i] });
    }
    return range;
}

Trades shrunk(Trades&& trades)
{
    trades.shrinkToFit();
    return std::move(trades);
}

} // namespace

PreprocessedTrades::RunningTotals::RunningTotals(const Trades& trades)
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

std::size_t PreprocessedTrades::RunningTotals::getBytes() const
{
    return mRunning.capacity() * sizeof(std::int64_t) + mRunningBuys.capacity() * sizeof(std::int64_t)
           + mBuyCounts.capacity() * sizeof(std::uint32_t);
}

PreprocessedTrades::PriceTree::PriceTree(const Trades& trades)
    : mSize(trades.size()),
      mBlocks((trades.size() + BlockSize - 1) / BlockSize),
      mNodes(2 * mBlocks)
{
    for (std::size_t b = 0; b < mBlocks; ++b) {
        mNodes[mBlocks + b] = scan(trades.price, b * BlockSize, std::min((b + 1) * BlockSize, mSize));
    }
    for (std::size_t k = mBlocks - 1; k > 0 && k < mBlocks; --k) {
        mNodes[k] = join(mNodes[2 * k], mNodes[2 * k + 1]);
    }
}

std::optional<ValueRange> PreprocessedTrades::PriceTree::getRange(const Trades& trades, IndexRange indices) const
{
    const std::size_t first = std::min(indices.first, mSize);
    const std::size_t last = std::min(indices.last, mSize);
    if (first >= last) {
        return std::nullopt;
    }
    // The blocks wholly inside; none if the range starts and ends in the same
    // block or the next.
    const std::size_t firstBlock = (first + BlockSize - 1) / BlockSize;
    const std::size_t lastBlock = last / BlockSize;
    if (firstBlock >= lastBlock) {
        return scan(trades.price, first, last);
    }

    std::optional<ValueRange> range;
    const auto add = [&range](ValueRange part) { range = range ? join(*range, part) : part; };
    if (first < firstBlock * BlockSize) {
        add(scan(trades.price, first, firstBlock * BlockSize));
    }
    if (lastBlock * BlockSize < last) {
        add(scan(trades.price, lastBlock * BlockSize, last));
    }
    // Climbs from both ends, taking each node that lies wholly inside.
    std::size_t left = firstBlock + mBlocks;
    std::size_t right = lastBlock + mBlocks;
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

PreprocessedTrades::PreprocessedTrades(Trades&& trades)
    : mTrades(shrunk(std::move(trades))),
      mTotals(mTrades),
      mPrices(mTrades)
{
}

std::optional<ValueRange> PreprocessedTrades::getPriceRange(IndexRange indices) const
{
    return mPrices.getRange(mTrades, indices);
}

PreprocessedTrades::Bytes PreprocessedTrades::getBytes() const
{
    return { .trades = mTrades.getBytes(), .totals = mTotals.getBytes(), .priceTree = mPrices.getBytes() };
}

} // namespace Core::Data
