#include "Gpu/Market/Style.hpp"

#include <doctest/doctest.h>

#include <cstddef>

namespace {

namespace Style = Gpu::Market::Style;
using Gpu::Market::Type::MarkerScale;
using Bound = Gpu::Market::Type::LegendSize::Bound;

} // namespace

TEST_CASE("On the area scale, four times the amount doubles the radius, within the limits")
{
    CHECK(Style::getMarkerRadiusCssPx(1.0, MarkerScale::Area) == doctest::Approx(Style::MarkerAreaRadiusAt1BtcCssPx));
    CHECK(Style::getMarkerRadiusCssPx(4.0, MarkerScale::Area) == doctest::Approx(2.0 * Style::MarkerAreaRadiusAt1BtcCssPx));
    CHECK(Style::getMarkerRadiusCssPx(1e-6, MarkerScale::Area) == Style::MarkerMinRadiusCssPx);
    CHECK(Style::getMarkerRadiusCssPx(1e6, MarkerScale::Area) == Style::MarkerMaxRadiusCssPx);
}

TEST_CASE("On the log scale, each power of ten adds the same radius, within the limits")
{
    const float step = Style::getMarkerRadiusCssPx(0.1, MarkerScale::Log) - Style::getMarkerRadiusCssPx(0.01, MarkerScale::Log);
    CHECK(step > 0.0f);
    CHECK(Style::getMarkerRadiusCssPx(10.0, MarkerScale::Log) - Style::getMarkerRadiusCssPx(1.0, MarkerScale::Log) == doctest::Approx(step));
    CHECK(Style::getMarkerRadiusCssPx(Style::MarkerLogLowest, MarkerScale::Log) == doctest::Approx(Style::MarkerMinRadiusCssPx));
    CHECK(Style::getMarkerRadiusCssPx(Style::MarkerLogHighest, MarkerScale::Log) == doctest::Approx(Style::MarkerMaxRadiusCssPx));
    CHECK(Style::getMarkerRadiusCssPx(1e-9, MarkerScale::Log) == Style::MarkerMinRadiusCssPx);
}

TEST_CASE("Amounts of zero or less get the smallest marker")
{
    for (const auto scale : { MarkerScale::Area, MarkerScale::Log }) {
        CHECK(Style::getMarkerRadiusCssPx(0.0, scale) == Style::MarkerMinRadiusCssPx);
        CHECK(Style::getMarkerRadiusCssPx(-1.0, scale) == Style::MarkerMinRadiusCssPx);
    }
}

TEST_CASE("A legend has the theme's marker colors")
{
    const Gpu::Market::Type::Theme theme{
        .plot = {}, .buy = { .red = 0.1f, .green = 0.2f, .blue = 0.9f }, .sell = { .red = 0.9f, .green = 0.6f, .blue = 0.1f }, .markerOpacity = 0.5f
    };
    const auto legend = Style::getLegend(MarkerScale::Area, theme);
    CHECK(legend.buy.blue == 0.9f);
    CHECK(legend.sell.green == 0.6f);
    CHECK(legend.opacity == 0.5f);
}

TEST_CASE("A legend spans the scale from where it stops shrinking to where it stops growing")
{
    for (const auto scale : { MarkerScale::Area, MarkerScale::Log }) {
        CAPTURE(scale);
        const auto legend = Style::getLegend(scale, {});

        const auto& sizes = legend.sizes;
        CHECK(sizes.size() >= 3);
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
            CHECK(Style::getMarkerRadiusCssPx(sizes[i].amount, scale) == doctest::Approx(sizes[i].radiusCssPx));
            if (i > 0) {
                CHECK(sizes[i].amount > sizes[i - 1].amount);
                CHECK(sizes[i].radiusCssPx > sizes[i - 1].radiusCssPx);
            }
            if (i > 0 && i + 1 < sizes.size()) {
                CHECK(sizes[i].bound == Bound::Exact);
            }
        }
        // Past the ends, sizes stay the same.
        CHECK(Style::getMarkerRadiusCssPx(sizes.front().amount / 10.0, scale) == Style::MarkerMinRadiusCssPx);
        CHECK(Style::getMarkerRadiusCssPx(sizes.back().amount * 10.0, scale) == Style::MarkerMaxRadiusCssPx);
    }
}
