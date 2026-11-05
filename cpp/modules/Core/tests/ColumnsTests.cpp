#include "Core/Data/Columns.hpp"
#include "Core/Data/PreprocessedTrades.hpp"
#include "Core/Data/Synthetic.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <utility>

namespace {

using Core::IndexRange;
using Core::Data::PreprocessedTrades;
using Core::Data::Side;
using Core::Data::Trades;

constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC

/** The lowest and highest price of the trades in `indices`, read one by one: what the price tree must agree with. */
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
    // Around the tree's blocks of 16 trades too.
    for (const std::size_t size : { 0, 1, 2, 7, 15, 16, 17, 31, 32, 33, 64, 1000 }) {
        CAPTURE(size);
        const auto day = Core::Data::generateSyntheticTrades(NoonUs, NoonUs + 3'600'000'000, 1);
        Trades trades;
        for (std::size_t i = 0; i < size && i < day.size(); ++i) {
            add(trades, day.timeUs[i] - NoonUs, day.price[i], day.amount[i], day.side[i]);
        }
        const PreprocessedTrades preprocessed(std::move(trades));
        std::mt19937 random(static_cast<std::uint32_t>(size));
        for (int check = 0; check < 200; ++check) {
            std::size_t first = size == 0 ? 0 : random() % (size + 1);
            std::size_t last = size == 0 ? 0 : random() % (size + 1);
            if (first > last) {
                std::swap(first, last);
            }
            const IndexRange indices{ .first = first, .last = last };
            const auto expected = scanPriceRange(preprocessed.getTrades(), indices);
            const auto actual = preprocessed.getPriceRange(indices);
            CHECK(actual.has_value() == expected.has_value());
            if (actual && expected) {
                CHECK(actual->low == expected->low);
                CHECK(actual->high == expected->high);
            }
        }
    }
}

TEST_CASE("The price tree agrees on every range, whichever blocks it starts and ends in")
{
    // Prices that rise and fall, so a range's low and high can be anywhere in it.
    Trades trades;
    for (std::size_t i = 0; i < 100; ++i) {
        add(trades, static_cast<std::int64_t>(i), 100.0 + static_cast<double>((i * 37) % 23), 1.0, Side::Buy);
    }
    const PreprocessedTrades preprocessed(std::move(trades));
    const auto& kept = preprocessed.getTrades();
    for (std::size_t first = 0; first <= kept.size(); ++first) {
        for (std::size_t last = first; last <= kept.size() + 1; ++last) {
            CAPTURE(first);
            CAPTURE(last);
            const IndexRange indices{ .first = first, .last = last };
            // Past the end counts as the end.
            const auto expected = scanPriceRange(kept, { .first = first, .last = std::min(last, kept.size()) });
            const auto actual = preprocessed.getPriceRange(indices);
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
    trades.instrument.amountDecimals = 8;
    const PreprocessedTrades preprocessed(std::move(trades));
    CHECK(preprocessed.getBuyAmountUnits({ 0, 3 }) == 40'000'000);
    CHECK(preprocessed.getAmountUnits({ 0, 3 }) - preprocessed.getBuyAmountUnits({ 0, 3 }) == 20'000'000);
    CHECK(preprocessed.getBuyCount({ 0, 3 }) == 2);
    CHECK(preprocessed.getBuyCount({ 1, 2 }) == 0);
}

TEST_CASE("Columns split the view by time, each trade in exactly one, empty ones left out")
{
    Trades trades;
    add(trades, 0, 100.0, 1.0, Side::Buy);   // column 0: [0, 25)
    add(trades, 24, 104.0, 0.5, Side::Sell);  // column 0
    add(trades, 25, 102.0, 2.0, Side::Buy);  // column 1 starts here, inclusive
    add(trades, 99, 101.0, 0.25, Side::Sell);  // column 3: [75, 100)
    add(trades, 100, 110.0, 1.0, Side::Buy);  // the view's end: left out
    const PreprocessedTrades preprocessed(std::move(trades));
    const Core::TimeRange view{ .startUs = NoonUs, .endUs = NoonUs + 100 };

    const auto columns = Core::Data::summarizeColumns(preprocessed, view, 4);
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
    const PreprocessedTrades preprocessed(
        Core::Data::generateSyntheticTrades(view.startUs - 60'000'000, view.endUs + 60'000'000, 2));
    const auto inView = Core::Data::getTradesIn(preprocessed.getTrades(), view);
    for (const std::size_t across : { 1, 3, 1000, 1601 }) {
        CAPTURE(across);
        const auto columns = Core::Data::summarizeColumns(preprocessed, view, across);
        std::size_t next = inView.first;
        std::int64_t units = 0;
        for (const auto& column : columns) {
            CHECK(column.trades.first == next);
            CHECK(column.index < across);
            next = column.trades.last;
            const auto& instrument = preprocessed.getTrades().instrument;
            units += instrument.toAmountUnits(column.buyAmount) + instrument.toAmountUnits(column.sellAmount);
        }
        CHECK(next == inView.last);
        CHECK(units == preprocessed.getAmountUnits(inView));
    }
}
