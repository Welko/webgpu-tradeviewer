#include "Gpu/Market/Marker.hpp"

#include <doctest/doctest.h>

#include <cstddef>

namespace {

namespace Marker = Gpu::Market::Marker;
using Gpu::Market::Type::MarkerScale;
using Bound = Gpu::Market::Type::LegendSize::Bound;

} // namespace

TEST_CASE("On the area scale, four times the amount doubles the radius, within the limits")
{
    CHECK(Marker::getRadius(1.0, MarkerScale::Area) == doctest::Approx(Marker::AreaRadiusAt1Btc));
    CHECK(Marker::getRadius(4.0, MarkerScale::Area) == doctest::Approx(2.0 * Marker::AreaRadiusAt1Btc));
    CHECK(Marker::getRadius(1e-6, MarkerScale::Area) == Marker::MinRadius);
    CHECK(Marker::getRadius(1e6, MarkerScale::Area) == Marker::MaxRadius);
}

TEST_CASE("On the log scale, each power of ten adds the same radius, within the limits")
{
    const float step = Marker::getRadius(0.1, MarkerScale::Log) - Marker::getRadius(0.01, MarkerScale::Log);
    CHECK(step > 0.0f);
    CHECK(Marker::getRadius(10.0, MarkerScale::Log) - Marker::getRadius(1.0, MarkerScale::Log) == doctest::Approx(step));
    CHECK(Marker::getRadius(Marker::LogLowest, MarkerScale::Log) == doctest::Approx(Marker::MinRadius));
    CHECK(Marker::getRadius(Marker::LogHighest, MarkerScale::Log) == doctest::Approx(Marker::MaxRadius));
    CHECK(Marker::getRadius(1e-9, MarkerScale::Log) == Marker::MinRadius);
}

TEST_CASE("Amounts of zero or less get the smallest marker")
{
    for (const auto scale : { MarkerScale::Area, MarkerScale::Log }) {
        CHECK(Marker::getRadius(0.0, scale) == Marker::MinRadius);
        CHECK(Marker::getRadius(-1.0, scale) == Marker::MinRadius);
    }
}

TEST_CASE("A legend has the theme's marker colours")
{
    const Gpu::Market::Type::Theme theme{
        .plot = {}, .buy = { .red = 0.1f, .green = 0.2f, .blue = 0.9f }, .sell = { .red = 0.9f, .green = 0.6f, .blue = 0.1f }, .markerOpacity = 0.5f
    };
    const auto legend = Marker::getLegend(MarkerScale::Area, theme);
    CHECK(legend.buy.blue == 0.9f);
    CHECK(legend.sell.green == 0.6f);
    CHECK(legend.opacity == 0.5f);
}

TEST_CASE("A legend spans the scale from where it stops shrinking to where it stops growing")
{
    for (const auto scale : { MarkerScale::Area, MarkerScale::Log }) {
        CAPTURE(scale);
        const auto legend = Marker::getLegend(scale, {});

        const auto& sizes = legend.sizes;
        CHECK(sizes.size() >= 3);
        if (sizes.size() < 3) {
            continue;
        }
        CHECK(sizes.front().bound == Bound::AtMost);
        CHECK(sizes.front().radius == Marker::MinRadius);
        CHECK(sizes.back().bound == Bound::AtLeast);
        CHECK(sizes.back().radius == Marker::MaxRadius);
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            CAPTURE(sizes[i].amount);
            // Each size is what the amount is drawn with.
            CHECK(Marker::getRadius(sizes[i].amount, scale) == doctest::Approx(sizes[i].radius));
            if (i > 0) {
                CHECK(sizes[i].amount > sizes[i - 1].amount);
                CHECK(sizes[i].radius > sizes[i - 1].radius);
            }
            if (i > 0 && i + 1 < sizes.size()) {
                CHECK(sizes[i].bound == Bound::Exact);
            }
        }
        // Past the ends, sizes stay the same.
        CHECK(Marker::getRadius(sizes.front().amount / 10.0, scale) == Marker::MinRadius);
        CHECK(Marker::getRadius(sizes.back().amount * 10.0, scale) == Marker::MaxRadius);
    }
}
