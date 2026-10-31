#include "Core/Data/Columns.hpp"
#include "Core/Data/PriceTree.hpp"
#include "Core/Data/Synthetic.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

namespace {

using Core::IndexRange;
using Core::Data::RunningTotals;
using Core::Data::PriceTree;
using Core::Data::Side;
using Core::Data::Trades;

constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC

/** The lowest and highest price of the trades in `indices`, read one by one: what PriceTree must agree with. */
std::optional<Core::ValueRange> scanPriceRange(const Trades& trades, IndexRange indices)
{
    if (indices.empty()) {
        return std::nullopt;
    }
    const auto begin = trades.price.begin();
    const auto [low, high] = std::minmax_element(begin + static_cast<std::ptrdiff_t>(indices.first),
                                                 begin + static_cast<std::ptrdiff_t>(indices.last));
    return Core::ValueRange{ .low = *low, .high = *high };
}

void add(Trades& trades, std::int64_t offsetUs, double price, double amount, Side side)
{
    trades.timeUs.push_back(NoonUs + offsetUs);
    trades.price.push_back(price);
    trades.amount.push_back(amount);
    trades.side.push_back(side);
}

} // namespace

TEST_CASE("The price tree gives the same range as reading every trade")
{
    for (const std::size_t size : { 0, 1, 2, 7, 64, 1000 }) {
        CAPTURE(size);
        const auto day = Core::Data::generateSyntheticTrades(NoonUs, NoonUs + 3'600'000'000, 1);
        Trades trades;
        for (std::size_t i = 0; i < size && i < day.size(); ++i) {
            add(trades, day.timeUs[i] - NoonUs, day.price[i], day.amount[i], day.side[i]);
        }
        const PriceTree tree(trades);
        std::mt19937 random(static_cast<std::uint32_t>(size));
        for (int check = 0; check < 200; ++check) {
            std::size_t first = size == 0 ? 0 : random() % (size + 1);
            std::size_t last = size == 0 ? 0 : random() % (size + 1);
            if (first > last) {
                std::swap(first, last);
            }
            const IndexRange indices{ .first = first, .last = last };
            const auto expected = scanPriceRange(trades, indices);
            const auto actual = tree.getRange(indices);
            CHECK(actual.has_value() == expected.has_value());
            if (actual && expected) {
                CHECK(actual->low == expected->low);
                CHECK(actual->high == expected->high);
            }
        }
    }
}

TEST_CASE("Running totals split amounts and counts by side, exactly")
{
    Trades trades;
    add(trades, 0, 100.0, 0.1, Side::Buy);
    add(trades, 1, 100.0, 0.2, Side::Sell);
    add(trades, 2, 100.0, 0.3, Side::Buy);
    const RunningTotals totals(trades);
    CHECK(totals.getBuyUnits({ 0, 3 }) == 40'000'000);
    CHECK(totals.getUnits({ 0, 3 }) - totals.getBuyUnits({ 0, 3 }) == 20'000'000);
    CHECK(totals.getBuyCount({ 0, 3 }) == 2);
    CHECK(totals.getBuyCount({ 1, 2 }) == 0);
}

TEST_CASE("Columns split the view by time, each trade in exactly one, empty ones left out")
{
    Trades trades;
    add(trades, 0, 100.0, 1.0, Side::Buy);   // column 0: [0, 25)
    add(trades, 24, 104.0, 0.5, Side::Sell);  // column 0
    add(trades, 25, 102.0, 2.0, Side::Buy);  // column 1 starts here, inclusive
    add(trades, 99, 101.0, 0.25, Side::Sell);  // column 3: [75, 100)
    add(trades, 100, 110.0, 1.0, Side::Buy);  // the view's end: left out
    const RunningTotals totals(trades);
    const PriceTree prices(trades);
    const Core::TimeRange view{ .startUs = NoonUs, .endUs = NoonUs + 100 };

    const auto columns = Core::Data::summarizeColumns(trades, totals, prices, view, 4);
    CHECK(columns.size() == 3);
    if (columns.size() != 3) {
        return;
    }
    CHECK(columns[0].index == 0);
    CHECK(columns[0].trades == IndexRange{ 0, 2 });
    CHECK(columns[0].price.low == 100.0);
    CHECK(columns[0].price.high == 104.0);
    CHECK(columns[0].buyAmount == 1.0);
    CHECK(columns[0].sellAmount == 0.5);
    CHECK(columns[0].buyCount == 1);
    CHECK(columns[1].index == 1);
    CHECK(columns[1].trades == IndexRange{ 2, 3 });
    CHECK(columns[2].index == 3);
    CHECK(columns[2].trades == IndexRange{ 3, 4 });
    CHECK(columns[2].buyCount == 0);
    CHECK(columns[2].sellAmount == 0.25);
}

TEST_CASE("Columns account for every trade in view, whatever the count")
{
    const Core::TimeRange view{ .startUs = NoonUs, .endUs = NoonUs + 3'600'000'000 };
    const auto trades = Core::Data::generateSyntheticTrades(view.startUs - 60'000'000, view.endUs + 60'000'000, 2);
    const RunningTotals totals(trades);
    const PriceTree prices(trades);
    const auto inView = Core::Data::getTradesIn(trades, view);
    for (const std::size_t across : { 1, 3, 1000, 1601 }) {
        CAPTURE(across);
        const auto columns = Core::Data::summarizeColumns(trades, totals, prices, view, across);
        std::size_t next = inView.first;
        std::int64_t units = 0;
        for (const auto& column : columns) {
            CHECK(column.trades.first == next);
            CHECK(column.index < across);
            next = column.trades.last;
            units += Core::Data::detail::toAmountUnits(column.buyAmount) + Core::Data::detail::toAmountUnits(column.sellAmount);
        }
        CHECK(next == inView.last);
        CHECK(units == totals.getUnits(inView));
    }
}
