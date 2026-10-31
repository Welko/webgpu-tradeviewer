#include "Core/Plot/Pick.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace {

using Core::Plot::PickRule;
using Core::Plot::Mapping;
using Core::Data::Trades;

constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC

// One pixel per microsecond across, one per dollar up: a trade at (t, p) is
// drawn t µs after noon from the left and 100 - p pixels from the top.
const Mapping Plot{
    .time = { .startUs = NoonUs, .endUs = NoonUs + 1000 }, .price = { .low = 0.0, .high = 100.0 }, .width = 1000.0, .height = 100.0
};
constexpr PickRule Rule{ .pickRadius = 24.0, .maxMarkerRadius = 16.0 };

/** Adds a trade `offsetUs` after noon. */
void add(Trades& trades, std::int64_t offsetUs, double price, double amount = 1.0)
{
    trades.timeUs.push_back(NoonUs + offsetUs);
    trades.price.push_back(price);
    trades.amount.push_back(amount);
    trades.side.push_back(Core::Data::Side::Buy);
}

/** Markers of radius 5, whatever the amount. */
float smallMarkers(double)
{
    return 5.0f;
}

std::optional<std::size_t> pick(const Trades& trades, double x, double y, const PickRule& rule = Rule)
{
    return Core::Plot::pickTrade(trades, { 0, trades.size() }, Plot, x, y, rule, smallMarkers);
}

} // namespace

TEST_CASE("The trade whose center is nearest on screen wins, counting both axes")
{
    Trades trades;
    add(trades, 500, 50.0);  // drawn at (500, 50)
    add(trades, 505, 70.0);  // (505, 30): 19 pixels from the pointer
    add(trades, 503, 90.0);  // (503, 10): nearest in time, but 39 pixels away
    CHECK(pick(trades, 503.0, 49.0) == 0);
    CHECK(pick(trades, 505.0, 33.0) == 1);
}

TEST_CASE("A center further than the pick radius isn't picked, unless the pointer is on its marker")
{
    Trades trades;
    add(trades, 500, 50.0);
    CHECK(pick(trades, 523.0, 50.0) == 0);
    CHECK(pick(trades, 525.0, 50.0) == std::nullopt);

    // With a pick radius smaller than the markers, the pointer must be on one.
    const PickRule tight{ .pickRadius = 2.0, .maxMarkerRadius = 5.0 };
    CHECK(pick(trades, 501.0, 50.0, tight) == 0);  // within the pick radius
    CHECK(pick(trades, 504.0, 50.0, tight) == 0);  // on the marker
    CHECK(pick(trades, 506.0, 50.0, tight) == std::nullopt);
}

TEST_CASE("Ties go to the later trade, which is drawn on top")
{
    Trades trades;
    add(trades, 500, 50.0);
    add(trades, 500, 50.0);
    CHECK(pick(trades, 510.0, 50.0) == 1);

    // Also among markers the pointer is on.
    const PickRule tight{ .pickRadius = 1.0, .maxMarkerRadius = 5.0 };
    add(trades, 506, 50.0);
    CHECK(pick(trades, 503.0, 50.0, tight) == 2);
}

TEST_CASE("Only visible trades inside the plot are picked")
{
    Trades trades;
    add(trades, 500, 50.0);
    add(trades, 600, 50.0);
    CHECK(Core::Plot::pickTrade(trades, { 1, 2 }, Plot, 500.0, 50.0, Rule, smallMarkers) == std::nullopt);
    CHECK(Core::Plot::pickTrade(trades, { 1, 2 }, Plot, 600.0, 50.0, Rule, smallMarkers) == 1);
    CHECK(pick(trades, -1.0, 50.0) == std::nullopt);
    CHECK(pick(trades, 500.0, 101.0) == std::nullopt);
    CHECK(Core::Plot::pickTrade(trades, { 0, 2 }, Mapping{ .time = Plot.time, .price = Plot.price, .width = 0.0, .height = 100.0 },
                                0.0, 50.0, Rule, smallMarkers)
          == std::nullopt);
}

namespace {

using Core::Data::ColumnSummary;

/** A column with trades from `low` to `high`; amounts don't matter for picking. */
ColumnSummary columnAt(std::size_t index, double low, double high)
{
    return { .index = index, .trades = { 0, 1 }, .price = { .low = low, .high = high }, .buyAmount = 1.0, .sellAmount = 0.0, .buyCount = 1 };
}

} // namespace

TEST_CASE("The nearest bar wins, measured to its nearest point, within the pick radius")
{
    // 100 columns of 10 pixels: column i's bar is at x = 10i + 5. The first
    // bar runs from y 40 (price 60) down to y 60 (price 40).
    const std::vector<ColumnSummary> columns{ columnAt(10, 40.0, 60.0), columnAt(12, 70.0, 80.0) };
    const auto pick = [&](double x, double y) { return Core::Plot::pickColumn(columns, 100, Plot, x, y, 24.0, 2.0); };
    CHECK(pick(105.0, 50.0) == 0);  // on the first bar
    CHECK(pick(105.0, 30.0) == 0);  // 10 pixels above its top
    CHECK(pick(125.0, 25.0) == 1);  // on the second bar
    CHECK(pick(105.0, 90.0) == std::nullopt);  // 30 pixels below its bottom
    CHECK(pick(160.0, 50.0) == std::nullopt);  // 35 pixels to the right of the second
}

TEST_CASE("A bar of a single price is still as tall as the minimum height")
{
    const std::vector<ColumnSummary> columns{ columnAt(10, 50.0, 50.0) };
    const auto bar = Core::Plot::getColumnBar(columns[0], 100, Plot, 4.0);
    CHECK(bar.centerX == 105.0);
    CHECK(bar.top == 48.0);
    CHECK(bar.bottom == 52.0);
}

TEST_CASE("Ties between bars go to the later column")
{
    const std::vector<ColumnSummary> columns{ columnAt(10, 40.0, 60.0), columnAt(11, 40.0, 60.0) };
    CHECK(Core::Plot::pickColumn(columns, 100, Plot, 110.0, 50.0, 24.0, 2.0) == 1);
}
