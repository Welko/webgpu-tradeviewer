#include "Viewer/View.hpp"

#include "TestTrades.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

namespace {

using Core::DayUs;
using TestTrades::NoonUs;

// A plot 1000 × 450 pixels.
constexpr Viewer::Layout Plot{
    .widthPx = 1100, .heightPx = 500, .pixelRatio = 1.0f, .insets = { .left = 50, .top = 10, .right = 50, .bottom = 40 }
};

/** 10 trades, one a millisecond from noon, priced 100 to 109. */
Core::Data::PreprocessedTrades makeTrades()
{
    return TestTrades::make(10, 1'000, [](std::size_t i) { return 100.0 + static_cast<double>(i); });
}

/** The first 8 ms after noon. */
Viewer::View makeView(const Core::Data::PreprocessedTrades& data)
{
    Viewer::View view;
    view.setTimeRange(data, { .startUs = NoonUs, .endUs = NoonUs + 8'000 });
    return view;
}

}  // namespace

TEST_CASE("A view holds the trades from its start up to, not including, its end")
{
    const auto data = makeTrades();
    Viewer::View view;
    view.setTimeRange(data, { .startUs = NoonUs + 2'000, .endUs = NoonUs + 5'000 });

    CHECK(view.getTrades() == Core::IndexRange{ .first = 2, .last = 5 });
    const auto& stats = view.getStats();
    CHECK(stats.count == 3);
    CHECK(stats.firstUs == NoonUs + 2'000);
    CHECK(stats.lastUs == NoonUs + 4'000);
    CHECK(stats.totalAmount == 3.0);
}

TEST_CASE("A view's prices reach 5% of its trades' price range past their lowest and highest")
{
    const auto data = makeTrades();
    Viewer::View view;
    view.setTimeRange(data, { .startUs = NoonUs + 2'000, .endUs = NoonUs + 5'000 });

    CHECK(view.getPriceRange().low == doctest::Approx(101.9));
    CHECK(view.getPriceRange().high == doctest::Approx(104.1));
}

TEST_CASE("A view of a single price still spans some prices, with the trades mid-height")
{
    const auto data = TestTrades::make(3, 1'000, [](std::size_t) { return 100.0; });
    const auto view = makeView(data);

    CHECK(view.getPriceRange().low == doctest::Approx(99.999));
    CHECK(view.getPriceRange().high == doctest::Approx(100.001));
}

TEST_CASE("A view without trades keeps the prices it had, and has no stats")
{
    const auto data = makeTrades();
    auto view = makeView(data);
    const auto prices = view.getPriceRange();

    view.setTimeRange(data, { .startUs = NoonUs + 20'000, .endUs = NoonUs + 30'000 });
    CHECK(view.getTrades().empty());
    CHECK(view.getStats().count == 0);
    CHECK(view.getStats().totalAmount == 0.0);
    CHECK(view.getPriceRange() == prices);
}

TEST_CASE("Zooming keeps the time under the pointer in place")
{
    const auto data = makeTrades();
    auto view = makeView(data);

    // A quarter of the way along the plot, 2 ms after noon.
    view.zoomAt(data, Plot, 50.0 + 250.0, 0.5);
    CHECK(view.getTimeRange() == Core::TimeRange{ .startUs = NoonUs + 1'000, .endUs = NoonUs + 5'000 });
    CHECK(view.getTrades() == Core::IndexRange{ .first = 1, .last = 5 });
}

TEST_CASE("Zooming stops at 1 ms in and 7 days out")
{
    const auto data = makeTrades();
    auto view = makeView(data);

    view.zoomAt(data, Plot, 550.0, 1e-6);
    CHECK(view.getTimeRange().getLengthUs() == 1'000);

    view.zoomAt(data, Plot, 550.0, 1e12);
    CHECK(view.getTimeRange().getLengthUs() == 7 * DayUs);
}

TEST_CASE("Dragging right by a quarter of the plot shows the time a quarter of the view earlier")
{
    const auto data = makeTrades();
    auto view = makeView(data);

    view.panBy(data, Plot, 250.0);
    CHECK(view.getTimeRange() == Core::TimeRange{ .startUs = NoonUs - 2'000, .endUs = NoonUs + 6'000 });
    CHECK(view.getTrades() == Core::IndexRange{ .first = 0, .last = 6 });
}

TEST_CASE("Without room for a plot, panning does nothing and zooming keeps the view's middle in place")
{
    const auto data = makeTrades();
    auto view = makeView(data);
    const Viewer::Layout noPlot{ .widthPx = 100, .heightPx = 500, .insets = Plot.insets };

    view.panBy(data, noPlot, 250.0);
    CHECK(view.getTimeRange() == Core::TimeRange{ .startUs = NoonUs, .endUs = NoonUs + 8'000 });

    view.zoomAt(data, noPlot, 250.0, 0.5);
    CHECK(view.getTimeRange() == Core::TimeRange{ .startUs = NoonUs + 2'000, .endUs = NoonUs + 6'000 });
}

TEST_CASE("A view maps its time and prices onto the plot, inside the insets")
{
    const auto data = makeTrades();
    const auto view = makeView(data);

    const auto mapping = view.getMapping(Plot);
    CHECK(mapping.time == view.getTimeRange());
    CHECK(mapping.price == view.getPriceRange());
    CHECK(mapping.width == 1000.0);
    CHECK(mapping.height == 450.0);
}
