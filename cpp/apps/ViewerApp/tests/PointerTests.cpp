#include "Viewer/Pointer.hpp"

#include "TestTrades.hpp"

#include "Core/Plot/Pick.hpp"
#include "Core/Plot/Sizes.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <variant>

namespace {

using Picked = Viewer::Pointer::Picked;
using TestTrades::NoonUs;

constexpr Core::Plot::Insets Insets{ .left = 50, .top = 10, .right = 50, .bottom = 40 };

/** A plot `widthPx` × 450 physical pixels inside the insets, at `pixelRatio`. */
Viewer::Layout makeLayout(std::uint32_t widthPx, float pixelRatio = 1.0f)
{
    return { .widthPx = widthPx + 100, .heightPx = 500, .pixelRatio = pixelRatio, .insets = Insets };
}

/** The view, its summaries and the pointer, as App keeps them. */
struct Parts
{
    Core::Data::PreprocessedTrades data;
    Viewer::Layout layout;
    Viewer::View view;
    Viewer::Summaries summaries;
    Viewer::Pointer pointer;

    /** `count` trades one every `spacingUs` from noon, priced 100 to 109 in turn, all in view. */
    Parts(std::size_t count, std::int64_t spacingUs, Viewer::Layout plotLayout)
        : data(TestTrades::make(count, spacingUs, [](std::size_t i) { return 100.0 + static_cast<double>(i % 10); })),
          layout(plotLayout)
    {
        view.setTimeRange(data, { .startUs = NoonUs, .endUs = NoonUs + (static_cast<std::int64_t>(count) * spacingUs) });
        summaries.update(data, view, layout);
    }

    void pointAt(double xPx, double yPx)
    {
        pointer.setPosition(xPx, yPx);
        update();
    }

    void update() { pointer.update(data, view, summaries, layout, Core::Plot::MarkerSizing{}); }

    /** Where trade `i`'s center is on the surface. */
    [[nodiscard]] double getX(std::size_t i) const { return Insets.left + view.getMapping(layout).getX(data.getTrades().timeUs[i]); }
    [[nodiscard]] double getY(std::size_t i) const { return Insets.top + view.getMapping(layout).getY(data.getTrades().price[i]); }
};

/** 4 trades 250 µs apart, 250 px apart on the plot, and too few for summaries. */
Parts makeSparse(float pixelRatio = 1.0f)
{
    return { 4, 250, makeLayout(1000, pixelRatio) };
}

}  // namespace

TEST_CASE("Before it's on the surface, the pointer picks nothing")
{
    auto parts = makeSparse();
    parts.update();

    CHECK(std::holds_alternative<std::monostate>(parts.pointer.getPicked()));
    CHECK_FALSE(parts.pointer.getHighlightedTrade());
    CHECK_FALSE(parts.pointer.getHighlightedColumn());
}

TEST_CASE("The pointer picks the trade it's on, placed on the surface")
{
    auto parts = makeSparse();
    parts.pointAt(parts.getX(1) + 3.0, parts.getY(1) - 4.0);

    const auto* trade = std::get_if<Picked::Trade>(&parts.pointer.getPicked());
    CHECK(trade);
    if (trade == nullptr)
    {
        return;
    }
    CHECK(trade->index == 1);
    CHECK(trade->timeUs == NoonUs + 250);
    CHECK(trade->price == 101.0);
    CHECK(trade->amount == 1.0);
    CHECK(trade->side == Core::Data::Side::Sell);
    CHECK(trade->centerXPx == doctest::Approx(parts.getX(1)));
    CHECK(trade->centerYPx == doctest::Approx(parts.getY(1)));
    CHECK(parts.pointer.getHighlightedTrade() == 1);
    CHECK_FALSE(parts.pointer.getHighlightedColumn());
}

TEST_CASE("The pointer picks trades up to 24 CSS px away, however many physical pixels that is")
{
    // 30 physical pixels are past 24 CSS px at a ratio of 1, and 15 CSS px at 2.
    auto parts = makeSparse(1.0f);
    parts.pointAt(parts.getX(1), parts.getY(1) + 30.0);
    CHECK_FALSE(parts.pointer.getHighlightedTrade());

    parts = makeSparse(2.0f);
    parts.pointAt(parts.getX(1), parts.getY(1) + 30.0);
    CHECK(parts.pointer.getHighlightedTrade() == 1);
}

TEST_CASE("Once the pointer leaves the surface, it picks nothing")
{
    auto parts = makeSparse();
    parts.pointAt(parts.getX(1), parts.getY(1));
    CHECK(parts.pointer.getHighlightedTrade());
    if (!parts.pointer.getHighlightedTrade())
    {
        return;
    }

    parts.pointer.clear();
    parts.update();
    CHECK(std::holds_alternative<std::monostate>(parts.pointer.getPicked()));
}

TEST_CASE("Over column summaries, the pointer picks the column it's on, and the time it covers")
{
    // 1000 trades a microsecond apart, across 20 columns of 50 µs.
    auto parts = Parts(1000, 1, makeLayout(20));
    CHECK(parts.summaries.getNumColumns() == 20);
    if (!(parts.summaries.getNumColumns() == 20))
    {
        return;
    }
    parts.pointAt(Insets.left + 3.5, Insets.top + 225.0);

    const auto* column = std::get_if<Picked::Column>(&parts.pointer.getPicked());
    CHECK(column);
    if (column == nullptr)
    {
        return;
    }
    CHECK(column->summary.index == 3);
    CHECK(column->summary.trades == Core::IndexRange{ .first = 150, .last = 200 });
    CHECK(column->time == Core::TimeRange{ .startUs = NoonUs + 150, .endUs = NoonUs + 200 });

    const auto bar = Core::Plot::getColumnBar(column->summary, 20, parts.view.getMapping(parts.layout), Core::Plot::ColumnMinHeightCssPx);
    CHECK(column->centerXPx == doctest::Approx(Insets.left + bar.centerX));
    CHECK(column->topPx == doctest::Approx(Insets.top + bar.top));
    CHECK(column->bottomPx == doctest::Approx(Insets.top + bar.bottom));

    CHECK(parts.pointer.getHighlightedColumn() == 3);
    CHECK_FALSE(parts.pointer.getHighlightedTrade());
}

TEST_CASE("Over column summaries, the pointer picks no single trade, even on one")
{
    auto parts = Parts(1000, 1, makeLayout(20));
    parts.pointAt(parts.getX(170), parts.getY(170));

    CHECK(std::holds_alternative<Picked::Column>(parts.pointer.getPicked()));
    CHECK_FALSE(parts.pointer.getHighlightedTrade());
}
