#include "Core/Data/TimeRange.hpp"
#include "Core/Data/Trades.hpp"

#include <doctest/doctest.h>

#include <cstdint>

namespace {

using Core::Data::AmountTotals;
using Core::Data::IndexRange;
using Core::Data::TimeRange;
using Core::Data::Trades;

constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC
constexpr std::int64_t HourUs = 3'600'000'000;

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

TEST_CASE("Zooming keeps the anchor in place")
{
    const TimeRange hour{ NoonUs, NoonUs + HourUs };

    CHECK(hour.zoomed(0.0, 0.5) == TimeRange{ NoonUs, NoonUs + HourUs / 2 });
    CHECK(hour.zoomed(1.0, 0.5) == TimeRange{ NoonUs + HourUs / 2, NoonUs + HourUs });
    CHECK(hour.zoomed(0.5, 2.0) == TimeRange{ NoonUs - HourUs / 2, NoonUs + HourUs * 3 / 2 });

    // Down to single microseconds, far from the epoch.
    const TimeRange tiny{ NoonUs, NoonUs + 10 };
    CHECK(tiny.zoomed(0.3, 0.5) == TimeRange{ NoonUs + 2, NoonUs + 7 });
}

TEST_CASE("Panning moves by a fraction of the length")
{
    const TimeRange hour{ NoonUs, NoonUs + HourUs };
    CHECK(hour.panned(0.25) == TimeRange{ NoonUs + HourUs / 4, NoonUs + HourUs * 5 / 4 });
    CHECK(hour.panned(-1.0) == TimeRange{ NoonUs - HourUs, NoonUs });
}

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

    const auto middle = getPriceRange(trades, { 1, 3 });
    CHECK(middle);
    if (middle) {
        CHECK(middle->low == 80.0);
        CHECK(middle->high == 120.0);
    }
    CHECK_FALSE(getPriceRange(trades, { 2, 2 }));
}

TEST_CASE("Totals of amounts are exact for any range")
{
    auto trades = tradesAt({ 1, 2, 3, 4 });
    // Doubles add 0.1 + 0.2 to 0.30000000000000004; units of 1e-8 don't.
    trades.amount = { 0.1, 0.2, 0.00000001, 12.5 };
    const AmountTotals totals(trades);

    CHECK(totals.getUnits({ 0, 2 }) == 30'000'000);
    CHECK(totals.getAmount({ 0, 2 }) == 0.3);
    CHECK(totals.getUnits({ 2, 3 }) == 1);
    CHECK(totals.getUnits({ 0, 4 }) == 1'280'000'001);
    CHECK(totals.getUnits({ 3, 3 }) == 0);
    CHECK(AmountTotals().getUnits({ 0, 0 }) == 0);
    CHECK(Core::Data::summarize(trades).totalAmount == totals.getAmount({ 0, 4 }));
}
