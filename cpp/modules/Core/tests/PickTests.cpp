#include "Core/Data/Pick.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

namespace {

using Core::Data::PickRule;
using Core::Data::PlotMapping;
using Core::Data::Trades;

constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC

// One pixel per microsecond across, one per dollar up: a trade at (t, p) is
// drawn t µs after noon from the left and 100 - p pixels from the top.
const PlotMapping Plot{
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
    return Core::Data::pickTrade(trades, { 0, trades.size() }, Plot, x, y, rule, smallMarkers);
}

} // namespace

TEST_CASE("The trade whose centre is nearest on screen wins, counting both axes")
{
    Trades trades;
    add(trades, 500, 50.0);  // drawn at (500, 50)
    add(trades, 505, 70.0);  // (505, 30): 19 pixels from the pointer
    add(trades, 503, 90.0);  // (503, 10): nearest in time, but 39 pixels away
    CHECK(pick(trades, 503.0, 49.0) == 0);
    CHECK(pick(trades, 505.0, 33.0) == 1);
}

TEST_CASE("A centre further than the pick radius isn't picked, unless the pointer is on its marker")
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
    CHECK(Core::Data::pickTrade(trades, { 1, 2 }, Plot, 500.0, 50.0, Rule, smallMarkers) == std::nullopt);
    CHECK(Core::Data::pickTrade(trades, { 1, 2 }, Plot, 600.0, 50.0, Rule, smallMarkers) == 1);
    CHECK(pick(trades, -1.0, 50.0) == std::nullopt);
    CHECK(pick(trades, 500.0, 101.0) == std::nullopt);
    CHECK(Core::Data::pickTrade(trades, { 0, 2 }, PlotMapping{ .time = Plot.time, .price = Plot.price, .width = 0.0, .height = 100.0 },
                                0.0, 50.0, Rule, smallMarkers)
          == std::nullopt);
}
