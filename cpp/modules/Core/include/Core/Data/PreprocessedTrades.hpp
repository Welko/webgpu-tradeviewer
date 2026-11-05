#pragma once

#include "Core/Data/Trades.hpp"
#include "Core/Range.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Core::Data {

/**
 * Trades, and what's worked out from them once so that questions about any
 * range of them are quick: running totals, and a tree of their prices. All
 * of it is built together from the trades and never changes afterwards, so
 * the parts always agree; other trades make another one. Ranges are indices
 * into getTrades().
 */
class PreprocessedTrades
{
public:
    /** The memory each part holds, room to grow included. */
    struct Bytes
    {
        std::size_t trades = 0;  // 25 per trade
        std::size_t totals = 0;  // 20 per trade
        std::size_t priceTree = 0;  // about 2 per trade
    };

    /** No trades. */
    PreprocessedTrades() = default;

    /**
     * Takes the trades, which are large: pass them with std::move, or copy
     * them on purpose. Gives back their room to grow, then works out the rest.
     */
    explicit PreprocessedTrades(Trades&& trades);

    [[nodiscard]] const Trades& getTrades() const { return mTrades; }

    /**
     * The lowest and highest price of the trades in `indices`, reading
     * O(log n) entries and at most 30 prices; nothing if `indices` is empty.
     */
    [[nodiscard]] std::optional<ValueRange> getPriceRange(IndexRange indices) const;

    // Totals, in constant time.

    /** The amount traded, in whole units of the finest amount step (see Instrument::toAmountUnits): exact, however it's added up. */
    [[nodiscard]] std::int64_t getAmountUnits(IndexRange indices) const { return mTotals.getAmountUnits(indices); }

    [[nodiscard]] double getAmount(IndexRange indices) const { return mTrades.instrument.toAmount(getAmountUnits(indices)); }

    /** Of the buys alone, in whole units of the finest amount step. */
    [[nodiscard]] std::int64_t getBuyAmountUnits(IndexRange indices) const { return mTotals.getBuyAmountUnits(indices); }

    [[nodiscard]] std::size_t getBuyCount(IndexRange indices) const { return mTotals.getBuyCount(indices); }

    [[nodiscard]] Bytes getBytes() const;

private:
    /** Amounts summed up to each trade, in all and of buys alone, and how many were buys. */
    class RunningTotals
    {
    public:
        RunningTotals() = default;

        explicit RunningTotals(const Trades& trades);

        [[nodiscard]] std::int64_t getAmountUnits(IndexRange indices) const { return mRunning[indices.last] - mRunning[indices.first]; }

        [[nodiscard]] std::int64_t getBuyAmountUnits(IndexRange indices) const
        {
            return mRunningBuys[indices.last] - mRunningBuys[indices.first];
        }

        [[nodiscard]] std::size_t getBuyCount(IndexRange indices) const
        {
            return mBuyCounts[indices.last] - mBuyCounts[indices.first];
        }

        /** 20 per trade. */
        [[nodiscard]] std::size_t getBytes() const;

    private:
        // Element i covers the first i trades.
        std::vector<std::int64_t> mRunning{ 0 };
        std::vector<std::int64_t> mRunningBuys{ 0 };
        std::vector<std::uint32_t> mBuyCounts{ 0 };
    };

    /**
     * A segment tree over blocks of trades. Its leaves are whole blocks, so
     * it takes about 2 bytes per trade; the trades at a range's ends, in
     * blocks it only partly covers, are read from the trades one by one.
     */
    class PriceTree
    {
    public:
        PriceTree() = default;

        explicit PriceTree(const Trades& trades);

        /** `trades` must be the ones the tree was built from. */
        [[nodiscard]] std::optional<ValueRange> getRange(const Trades& trades, IndexRange indices) const;

        /** 32 per block. */
        [[nodiscard]] std::size_t getBytes() const { return mNodes.capacity() * sizeof(ValueRange); }

    private:
        // Trades per leaf: at most twice this, less 2, are read one by one.
        static constexpr std::size_t BlockSize = 16;

        std::size_t mSize = 0;  // trades
        std::size_t mBlocks = 0;
        // mNodes[mBlocks + b] spans block b's prices; mNodes[k], for
        // 0 < k < mBlocks, spans mNodes[2k] and mNodes[2k + 1].
        std::vector<ValueRange> mNodes;
    };

    Trades mTrades;
    RunningTotals mTotals;
    PriceTree mPrices;
};

} // namespace Core::Data
