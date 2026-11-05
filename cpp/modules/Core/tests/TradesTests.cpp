#include "Core/Data/PreprocessedTrades.hpp"
#include "Core/Data/Trades.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using Core::Data::PreprocessedTrades;
using Core::IndexRange;
using Core::Data::Trades;

constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC

/** Trades at the given times, priced by their position. */
Trades tradesAt(std::initializer_list<std::int64_t> times)
{
    Trades trades;
    for (const auto timeUs : times) {
        trades.timeUs.push_back(timeUs);
        trades.price.push_back(100.0 + static_cast<double>(trades.price.size()));
        trades.amount.push_back(1.0);
        trades.side.push_back(Core::Data::Side::Buy);
    }
    return trades;
}

} // namespace

TEST_CASE("A time range holds its start but not its end")
{
    const auto trades = tradesAt({ NoonUs - 1, NoonUs, NoonUs, NoonUs + 5, NoonUs + 10, NoonUs + 11 });

    CHECK(getTradesIn(trades, { NoonUs, NoonUs + 10 }) == IndexRange{ 1, 4 });
    CHECK(getTradesIn(trades, { NoonUs, NoonUs + 11 }) == IndexRange{ 1, 5 });
    CHECK(getTradesIn(trades, { NoonUs + 1, NoonUs + 5 }).empty());
    CHECK(getTradesIn(trades, { NoonUs - 100, NoonUs + 100 }) == IndexRange{ 0, 6 });
    CHECK(getTradesIn(Trades{}, { NoonUs, NoonUs + 10 }).empty());
}

TEST_CASE("The price range covers only the given trades")
{
    auto trades = tradesAt({ 1, 2, 3, 4 });
    trades.price = { 50.0, 120.0, 80.0, 10.0 };
    const PreprocessedTrades preprocessed(std::move(trades));

    const auto middle = preprocessed.getPriceRange({ 1, 3 });
    CHECK(middle);
    if (middle) {
        CHECK(middle->low == 80.0);
        CHECK(middle->high == 120.0);
    }
    CHECK_FALSE(preprocessed.getPriceRange({ 2, 2 }));
}

TEST_CASE("Totals of amounts are exact for any range")
{
    auto trades = tradesAt({ 1, 2, 3, 4 });
    // Doubles add 0.1 + 0.2 to 0.30000000000000004; units of 1e-8 don't.
    trades.amount = { 0.1, 0.2, 0.00000001, 12.5 };
    trades.instrument.amountDecimals = 8;
    const PreprocessedTrades preprocessed(std::move(trades));

    CHECK(preprocessed.getAmountUnits({ 0, 2 }) == 30'000'000);
    CHECK(preprocessed.getAmount({ 0, 2 }) == 0.3);
    CHECK(preprocessed.getAmountUnits({ 2, 3 }) == 1);
    CHECK(preprocessed.getAmountUnits({ 0, 4 }) == 1'280'000'001);
    CHECK(preprocessed.getAmountUnits({ 3, 3 }) == 0);
    CHECK(PreprocessedTrades().getAmountUnits({ 0, 0 }) == 0);
    CHECK(Core::Data::getStats(preprocessed.getTrades()).totalAmount == preprocessed.getAmount({ 0, 4 }));
}

TEST_CASE("Amount percentiles are the amounts that far up the sorted trades")
{
    // 1 to 1000, shuffled.
    Trades trades;
    for (std::size_t i = 0; i < 1000; ++i) {
        trades.timeUs.push_back(NoonUs + static_cast<std::int64_t>(i));
        trades.price.push_back(100.0);
        trades.amount.push_back(static_cast<double>((i * 337) % 1000 + 1));
        trades.side.push_back(Core::Data::Side::Buy);
    }
    CHECK(Core::Data::getAmountPercentiles(trades, 0.01, 0.999) == Core::ValueRange{ .low = 10.0, .high = 999.0 });
    CHECK(Core::Data::getAmountPercentiles(trades, 0.5, 1.0) == Core::ValueRange{ .low = 500.0, .high = 1000.0 });
    CHECK(Core::Data::getAmountPercentiles(trades, 0.0, 0.0) == Core::ValueRange{ .low = 1.0, .high = 1.0 });
    CHECK(Core::Data::getAmountPercentiles(Trades{}, 0.01, 0.999) == Core::ValueRange{ .low = 0.0, .high = 0.0 });
}
