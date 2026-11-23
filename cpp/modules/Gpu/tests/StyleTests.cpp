#include "Gpu/Market/Style.hpp"

#include "Core/Plot/Sizes.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <initializer_list>

namespace {

namespace Style = Gpu::Market::Style;
using Core::Plot::MarkerScale;
using Bound = Gpu::Market::Type::LegendSize::Bound;

// As fitted to the real day, in BTC.
constexpr double LowAmount = 2e-5;
constexpr double HighAmount = 5.9;
constexpr Core::Plot::MarkerSizing Area{ .scale = MarkerScale::Area, .lowAmount = LowAmount, .highAmount = HighAmount };
constexpr Core::Plot::MarkerSizing Log{ .scale = MarkerScale::Log, .lowAmount = LowAmount, .highAmount = HighAmount };

}  // namespace

TEST_CASE("A legend has the theme's marker colors")
{
    const Gpu::Market::Type::Theme theme{ .plot = {},
                                          .buy = { .red = 0.1f, .green = 0.2f, .blue = 0.9f },
                                          .sell = { .red = 0.9f, .green = 0.6f, .blue = 0.1f },
                                          .markerOpacity = 0.5f };
    const auto legend = Style::getLegend(Area, theme);
    CHECK(legend.buy.blue == 0.9f);
    CHECK(legend.sell.green == 0.6f);
    CHECK(legend.opacity == 0.5f);
}

TEST_CASE("A legend spans the scale from where it stops shrinking to where it stops growing")
{
    const std::initializer_list<Core::Plot::MarkerSizing> sizings{
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
    for (const auto& sizing : sizings)
    {
        CAPTURE(sizing.scale);
        CAPTURE(sizing.highAmount);
        const auto legend = Style::getLegend(sizing, {});

        const auto& sizes = legend.sizes;
        CHECK(sizes.size() >= 3);
        CHECK(sizes.size() <= 6);
        if (sizes.size() < 3)
        {
            continue;
        }
        CHECK(sizes.front().bound == Bound::AtMost);
        CHECK(sizes.front().radiusCssPx == Core::Plot::MarkerMinRadiusCssPx);
        CHECK(sizes.back().bound == Bound::AtLeast);
        CHECK(sizes.back().radiusCssPx == Core::Plot::MarkerMaxRadiusCssPx);
        for (std::size_t i = 0; i < sizes.size(); ++i)
        {
            CAPTURE(sizes[i].amount);
            // Each size is what the amount is drawn with.
            CHECK(sizing.getRadiusCssPx(sizes[i].amount) == doctest::Approx(sizes[i].radiusCssPx));
            if (i > 0)
            {
                CHECK(sizes[i].amount > sizes[i - 1].amount);
                CHECK(sizes[i].radiusCssPx > sizes[i - 1].radiusCssPx);
            }
            if (i > 0 && i + 1 < sizes.size())
            {
                CHECK(sizes[i].bound == Bound::Exact);
            }
        }
        // Past the ends, sizes stay the same.
        CHECK(sizing.getRadiusCssPx(sizes.front().amount / 10.0) == Core::Plot::MarkerMinRadiusCssPx);
        CHECK(sizing.getRadiusCssPx(sizes.back().amount * 10.0) == Core::Plot::MarkerMaxRadiusCssPx);
    }
}
