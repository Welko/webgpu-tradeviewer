#include "Core/Plot/Sizes.hpp"

#include "Core/Data/Trades.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>

namespace {

using Core::Plot::MarkerScale;
using Core::Plot::MarkerSizing;

// As fitted to the real day, in BTC.
constexpr double LowAmount = 2e-5;
constexpr double HighAmount = 5.9;
constexpr MarkerSizing Area{ .scale = MarkerScale::Area, .lowAmount = LowAmount, .highAmount = HighAmount };
constexpr MarkerSizing Log{ .scale = MarkerScale::Log, .lowAmount = LowAmount, .highAmount = HighAmount };

Core::Data::Trades tradesOf(std::initializer_list<double> amounts)
{
    Core::Data::Trades trades;
    for (const double amount : amounts)
    {
        trades.timeUs.push_back(static_cast<std::int64_t>(trades.size()));
        trades.price.push_back(100.0);
        trades.amount.push_back(amount);
        trades.side.push_back(Core::Data::Side::Buy);
    }
    return trades;
}

}  // namespace

TEST_CASE("Marker sizing is fitted to the trades' amounts, keeping the scale, however few or odd")
{
    Core::Data::Trades trades;
    for (int amount = 1; amount <= 1000; ++amount)
    {
        trades.timeUs.push_back(amount);
        trades.price.push_back(100.0);
        trades.amount.push_back(amount);
        trades.side.push_back(Core::Data::Side::Buy);
    }
    CHECK(Core::Plot::fitMarkerSizing(MarkerScale::Log, trades)
          == MarkerSizing{ .scale = MarkerScale::Log, .lowAmount = 10.0, .highAmount = 999.0 });
    // One amount: the log scale gets ten times below it.
    CHECK(Core::Plot::fitMarkerSizing(MarkerScale::Area, tradesOf({ 100.0, 100.0 }))
          == MarkerSizing{ .scale = MarkerScale::Area, .lowAmount = 10.0, .highAmount = 100.0 });
    // No trades: the default.
    CHECK(Core::Plot::fitMarkerSizing(MarkerScale::Log, {}) == MarkerSizing{ .scale = MarkerScale::Log });
    // Extremes stay within what the shader's 32-bit floats can hold, ratio included.
    for (const auto& trades : { tradesOf({ 1e-300, 1e-300 }), tradesOf({ 1e-300, 1e15 }), tradesOf({ 1e15, 1e15 }) })
    {
        const auto sizing = Core::Plot::fitMarkerSizing(MarkerScale::Log, trades);
        CHECK(sizing.lowAmount > 1e-37);
        CHECK(sizing.highAmount < 1e37);
        CHECK(sizing.highAmount / sizing.lowAmount < 1e37);
        CHECK(sizing.highAmount >= 10.0 * sizing.lowAmount);
    }
}

TEST_CASE("On the area scale, four times the amount doubles the radius, the largest at the high amount")
{
    CHECK(Area.getRadiusCssPx(HighAmount) == doctest::Approx(Core::Plot::MarkerMaxRadiusCssPx));
    CHECK(Area.getRadiusCssPx(HighAmount / 4.0) == doctest::Approx(Core::Plot::MarkerMaxRadiusCssPx / 2.0));
    CHECK(Area.getRadiusCssPx(1e-9) == Core::Plot::MarkerMinRadiusCssPx);
    CHECK(Area.getRadiusCssPx(1e6) == Core::Plot::MarkerMaxRadiusCssPx);
}

TEST_CASE("On the log scale, each power of ten adds the same radius, from the low amount to the high one")
{
    const float step = Log.getRadiusCssPx(0.1) - Log.getRadiusCssPx(0.01);
    CHECK(step > 0.0f);
    CHECK(Log.getRadiusCssPx(1.0) - Log.getRadiusCssPx(0.1) == doctest::Approx(step));
    CHECK(Log.getRadiusCssPx(LowAmount) == doctest::Approx(Core::Plot::MarkerMinRadiusCssPx));
    CHECK(Log.getRadiusCssPx(HighAmount) == doctest::Approx(Core::Plot::MarkerMaxRadiusCssPx));
    CHECK(Log.getRadiusCssPx(1e-9) == Core::Plot::MarkerMinRadiusCssPx);
}

TEST_CASE("Sizes follow the market: a thousand times the amounts, the same sizes")
{
    for (const auto scale : { MarkerScale::Area, MarkerScale::Log })
    {
        CAPTURE(scale);
        const MarkerSizing sizing{ .scale = scale, .lowAmount = LowAmount, .highAmount = HighAmount };
        const MarkerSizing larger{ .scale = scale, .lowAmount = LowAmount * 1000.0, .highAmount = HighAmount * 1000.0 };
        for (const double amount : { 1e-4, 0.01, 1.0 })
        {
            CHECK(larger.getRadiusCssPx(amount * 1000.0) == doctest::Approx(sizing.getRadiusCssPx(amount)));
        }
    }
}

TEST_CASE("Amounts of zero or less get the smallest marker")
{
    for (const auto& sizing : { Area, Log })
    {
        CHECK(sizing.getRadiusCssPx(0.0) == Core::Plot::MarkerMinRadiusCssPx);
        CHECK(sizing.getRadiusCssPx(-1.0) == Core::Plot::MarkerMinRadiusCssPx);
    }
}
