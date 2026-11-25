#include "Viewer/Axes.hpp"

#include "TestTrades.hpp"

#include "Core/Plot/Axis.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using Core::Plot::Axis::Tick;
using TestTrades::NoonUs;

/** A plot `widthPx` × `heightPx` physical pixels inside insets, at `pixelRatio`. */
Viewer::Layout makeLayout(std::uint32_t widthPx, std::uint32_t heightPx, float pixelRatio)
{
    return { .widthPx = widthPx + 100,
             .heightPx = heightPx + 50,
             .pixelRatio = pixelRatio,
             .insets = { .left = 50, .top = 10, .right = 50, .bottom = 40 } };
}

void checkSame(const std::vector<Tick>& actual, const std::vector<Tick>& expected)
{
    CHECK_FALSE(expected.empty());
    CHECK(actual.size() == expected.size());
    if (!(actual.size() == expected.size()))
    {
        return;
    }
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        CHECK(actual[i].fraction == expected[i].fraction);
        CHECK(actual[i].label == expected[i].label);
    }
}

/** Checks that `axes` asks for at most `timeCount` time ticks and `priceCount` price ticks. */
void checkCounts(const Viewer::Axes& axes, const Viewer::View& view, std::size_t timeCount, std::size_t priceCount)
{
    checkSame(axes.getTimes(), Core::Plot::Axis::getTimeTicks(view.getTimeRange(), timeCount));
    checkSame(axes.getPrices(), Core::Plot::Axis::getNumberTicks(view.getPriceRange(), priceCount));
}

/** 10 trades over the first 10 seconds after noon, priced 100 to 109. */
Viewer::View makeView(const Core::Data::PreprocessedTrades& data)
{
    Viewer::View view;
    view.setTimeRange(data, { .startUs = NoonUs, .endUs = NoonUs + 10'000'000 });
    return view;
}

}  // namespace

TEST_CASE("Axes ask for as many ticks as fit 120 CSS px apart in time and 50 CSS px apart in price")
{
    const auto data = TestTrades::make(10, 1'000'000, [](std::size_t i) { return 100.0 + static_cast<double>(i); });
    const auto view = makeView(data);
    Viewer::Axes axes;

    axes.update(view, makeLayout(1000, 450, 1.0f));
    checkCounts(axes, view, 8, 9);

    // The same physical pixels at twice the density are half the CSS pixels.
    axes.update(view, makeLayout(1000, 450, 2.0f));
    checkCounts(axes, view, 4, 4);
}

TEST_CASE("A plot too small for two ticks still asks for two per axis")
{
    const auto data = TestTrades::make(10, 1'000'000, [](std::size_t i) { return 100.0 + static_cast<double>(i); });
    const auto view = makeView(data);
    Viewer::Axes axes;

    axes.update(view, makeLayout(10, 10, 1.0f));
    checkCounts(axes, view, 2, 2);
}
