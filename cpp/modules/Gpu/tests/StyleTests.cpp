#include "Gpu/Market/Style.hpp"

#include "Core/Data/Trades.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace {

namespace Style = Gpu::Market::Style;
using Gpu::Market::Type::MarkerScale;
using Bound = Gpu::Market::Type::LegendSize::Bound;

// As fitted to the real day, in BTC.
constexpr double LowAmount = 2e-5;
constexpr double HighAmount = 5.9;
constexpr Style::MarkerSizing Area{ .scale = MarkerScale::Area, .lowAmount = LowAmount, .highAmount = HighAmount };
constexpr Style::MarkerSizing Log{ .scale = MarkerScale::Log, .lowAmount = LowAmount, .highAmount = HighAmount };

Core::Data::Trades tradesOf(std::initializer_list<double> amounts)
{
    Core::Data::Trades trades;
    for (const double amount : amounts) {
        trades.timeUs.push_back(static_cast<std::int64_t>(trades.size()));
        trades.price.push_back(100.0);
        trades.amount.push_back(amount);
        trades.side.push_back(Core::Data::Side::Buy);
    }
    return trades;
}

} // namespace

TEST_CASE("Marker sizing is fitted to the trades' amounts, keeping the scale, however few or odd")
{
    Core::Data::Trades trades;
    for (int amount = 1; amount <= 1000; ++amount) {
        trades.timeUs.push_back(amount);
        trades.price.push_back(100.0);
        trades.amount.push_back(amount);
        trades.side.push_back(Core::Data::Side::Buy);
    }
    CHECK(Style::fitMarkerSizing(MarkerScale::Log, trades)
          == Style::MarkerSizing{ .scale = MarkerScale::Log, .lowAmount = 10.0, .highAmount = 999.0 });
    // One amount: the log scale gets ten times below it.
    CHECK(Style::fitMarkerSizing(MarkerScale::Area, tradesOf({ 100.0, 100.0 }))
          == Style::MarkerSizing{ .scale = MarkerScale::Area, .lowAmount = 10.0, .highAmount = 100.0 });
    // No trades: the default.
    CHECK(Style::fitMarkerSizing(MarkerScale::Log, {}) == Style::MarkerSizing{ .scale = MarkerScale::Log });
    // Extremes stay within what the shader's 32-bit floats can hold, ratio included.
    for (const auto& trades : { tradesOf({ 1e-300, 1e-300 }), tradesOf({ 1e-300, 1e15 }), tradesOf({ 1e15, 1e15 }) }) {
        const auto sizing = Style::fitMarkerSizing(MarkerScale::Log, trades);
        CHECK(sizing.lowAmount > 1e-37);
        CHECK(sizing.highAmount < 1e37);
        CHECK(sizing.highAmount / sizing.lowAmount < 1e37);
        CHECK(sizing.highAmount >= 10.0 * sizing.lowAmount);
    }
}

TEST_CASE("On the area scale, four times the amount doubles the radius, the largest at the high amount")
{
    CHECK(Area.getRadiusCssPx(HighAmount) == doctest::Approx(Style::MarkerMaxRadiusCssPx));
    CHECK(Area.getRadiusCssPx(HighAmount / 4.0) == doctest::Approx(Style::MarkerMaxRadiusCssPx / 2.0));
    CHECK(Area.getRadiusCssPx(1e-9) == Style::MarkerMinRadiusCssPx);
    CHECK(Area.getRadiusCssPx(1e6) == Style::MarkerMaxRadiusCssPx);
}

TEST_CASE("On the log scale, each power of ten adds the same radius, from the low amount to the high one")
{
    const float step = Log.getRadiusCssPx(0.1) - Log.getRadiusCssPx(0.01);
    CHECK(step > 0.0f);
    CHECK(Log.getRadiusCssPx(1.0) - Log.getRadiusCssPx(0.1) == doctest::Approx(step));
    CHECK(Log.getRadiusCssPx(LowAmount) == doctest::Approx(Style::MarkerMinRadiusCssPx));
    CHECK(Log.getRadiusCssPx(HighAmount) == doctest::Approx(Style::MarkerMaxRadiusCssPx));
    CHECK(Log.getRadiusCssPx(1e-9) == Style::MarkerMinRadiusCssPx);
}

TEST_CASE("Sizes follow the market: a thousand times the amounts, the same sizes")
{
    for (const auto scale : { MarkerScale::Area, MarkerScale::Log }) {
        CAPTURE(scale);
        const Style::MarkerSizing sizing{ .scale = scale, .lowAmount = LowAmount, .highAmount = HighAmount };
        const Style::MarkerSizing larger{ .scale = scale, .lowAmount = LowAmount * 1000.0, .highAmount = HighAmount * 1000.0 };
        for (const double amount : { 1e-4, 0.01, 1.0 }) {
            CHECK(larger.getRadiusCssPx(amount * 1000.0) == doctest::Approx(sizing.getRadiusCssPx(amount)));
        }
    }
}

TEST_CASE("Amounts of zero or less get the smallest marker")
{
    for (const auto& sizing : { Area, Log }) {
        CHECK(sizing.getRadiusCssPx(0.0) == Style::MarkerMinRadiusCssPx);
        CHECK(sizing.getRadiusCssPx(-1.0) == Style::MarkerMinRadiusCssPx);
    }
}

TEST_CASE("A legend has the theme's marker colors")
{
    const Gpu::Market::Type::Theme theme{
        .plot = {}, .buy = { .red = 0.1f, .green = 0.2f, .blue = 0.9f }, .sell = { .red = 0.9f, .green = 0.6f, .blue = 0.1f }, .markerOpacity = 0.5f
    };
    const auto legend = Style::getLegend(Area, theme);
    CHECK(legend.buy.blue == 0.9f);
    CHECK(legend.sell.green == 0.6f);
    CHECK(legend.opacity == 0.5f);
}

TEST_CASE("A legend spans the scale from where it stops shrinking to where it stops growing")
{
    const std::initializer_list<Style::MarkerSizing> sizings{
        Area,
        Log,
        { .scale = MarkerScale::Area },
        { .scale = MarkerScale::Log },
        // A very wide scale.
        { .scale = MarkerScale::Log, .lowAmount = 1e-15, .highAmount = 1e15 },
        // A scale of one decade, as a single amount gets.
        { .scale = MarkerScale::Area, .lowAmount = 10.0, .highAmount = 100.0 },
        { .scale = MarkerScale::Log, .lowAmount = 10.0, .highAmount = 100.0 },
    };
    for (const auto& sizing : sizings) {
        CAPTURE(sizing.scale);
        CAPTURE(sizing.highAmount);
        const auto legend = Style::getLegend(sizing, {});

        const auto& sizes = legend.sizes;
        CHECK(sizes.size() >= 3);
        CHECK(sizes.size() <= 6);
        if (sizes.size() < 3) {
            continue;
        }
        CHECK(sizes.front().bound == Bound::AtMost);
        CHECK(sizes.front().radiusCssPx == Style::MarkerMinRadiusCssPx);
        CHECK(sizes.back().bound == Bound::AtLeast);
        CHECK(sizes.back().radiusCssPx == Style::MarkerMaxRadiusCssPx);
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            CAPTURE(sizes[i].amount);
            // Each size is what the amount is drawn with.
            CHECK(sizing.getRadiusCssPx(sizes[i].amount) == doctest::Approx(sizes[i].radiusCssPx));
            if (i > 0) {
                CHECK(sizes[i].amount > sizes[i - 1].amount);
                CHECK(sizes[i].radiusCssPx > sizes[i - 1].radiusCssPx);
            }
            if (i > 0 && i + 1 < sizes.size()) {
                CHECK(sizes[i].bound == Bound::Exact);
            }
        }
        // Past the ends, sizes stay the same.
        CHECK(sizing.getRadiusCssPx(sizes.front().amount / 10.0) == Style::MarkerMinRadiusCssPx);
        CHECK(sizing.getRadiusCssPx(sizes.back().amount * 10.0) == Style::MarkerMaxRadiusCssPx);
    }
}
