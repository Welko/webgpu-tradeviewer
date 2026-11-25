#include "Viewer/Summaries.hpp"

#include "TestTrades.hpp"

#include "Core/Data/Columns.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

namespace {

using TestTrades::NoonUs;

/** A plot `widthPx` physical pixels wide, at `pixelRatio`. */
Viewer::Layout makeLayout(std::uint32_t widthPx, float pixelRatio = 1.0f)
{
    return {
        .widthPx = widthPx + 100, .heightPx = 500, .pixelRatio = pixelRatio, .insets = { .left = 50, .top = 10, .right = 50, .bottom = 40 }
    };
}

/** `count` trades, one a microsecond from noon, priced 100 to 109 in turn. */
Core::Data::PreprocessedTrades makeTrades(std::size_t count)
{
    return TestTrades::make(count, 1, [](std::size_t i) { return 100.0 + static_cast<double>(i % 10); });
}

/** All the trades. */
Viewer::View makeView(const Core::Data::PreprocessedTrades& data)
{
    Viewer::View view;
    view.setTimeRange(data, { .startUs = NoonUs, .endUs = NoonUs + static_cast<std::int64_t>(data.getTrades().size()) });
    return view;
}

}  // namespace

TEST_CASE("Past 32 trades per CSS px column on average, columns summarize the view")
{
    // 960 trades are 32 per column across 30 columns, and 33.1 across 29.
    const auto data = makeTrades(960);
    const auto view = makeView(data);
    Viewer::Summaries summaries;

    CHECK_FALSE(summaries.update(data, view, makeLayout(30)));
    CHECK(summaries.getColumnCount() == 0);
    CHECK(summaries.getColumns().empty());

    CHECK(summaries.update(data, view, makeLayout(29)));
    CHECK(summaries.getColumnCount() == 29);
    const auto expected = Core::Data::summarizeColumns(data, view.getTimeRange(), 29);
    CHECK(summaries.getColumns().size() == expected.size());
    if (!(summaries.getColumns().size() == expected.size()))
    {
        return;
    }
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        CHECK(summaries.getColumns()[i].index == expected[i].index);
        CHECK(summaries.getColumns()[i].trades == expected[i].trades);
    }
}

TEST_CASE("Columns are a CSS px wide, so a pixel ratio of 2 halves how many there are")
{
    const auto data = makeTrades(1000);
    const auto view = makeView(data);
    Viewer::Summaries summaries;

    CHECK(summaries.update(data, view, makeLayout(40, 2.0f)));
    CHECK(summaries.getColumnCount() == 20);
}

TEST_CASE("update says whether there are new columns, or columns no longer")
{
    const auto data = makeTrades(1000);
    const auto view = makeView(data);
    Viewer::Summaries summaries;

    CHECK(summaries.update(data, view, makeLayout(20)));
    CHECK(summaries.update(data, view, makeLayout(20)));

    // 1000 trades are fewer than 32 per column across 40.
    CHECK(summaries.update(data, view, makeLayout(40)));
    CHECK(summaries.getColumnCount() == 0);
    CHECK(summaries.getColumns().empty());
    CHECK_FALSE(summaries.update(data, view, makeLayout(40)));
}

TEST_CASE("Summaries can be turned off, and their threshold moved")
{
    const auto data = makeTrades(1000);
    const auto view = makeView(data);
    const auto layout = makeLayout(20);
    Viewer::Summaries summaries;

    summaries.setAllowed(false);
    CHECK_FALSE(summaries.update(data, view, layout));
    CHECK(summaries.getColumnCount() == 0);

    // 50 trades per column.
    summaries.setAllowed(true);
    summaries.setThreshold(50.0);
    CHECK_FALSE(summaries.update(data, view, layout));
    summaries.setThreshold(49.0);
    CHECK(summaries.update(data, view, layout));
    CHECK(summaries.getColumnCount() == 20);
}

TEST_CASE("Without room for a plot, there are no columns")
{
    const auto data = makeTrades(1000);
    const auto view = makeView(data);
    Viewer::Summaries summaries;

    CHECK_FALSE(summaries.update(data, view, makeLayout(0)));
    CHECK(summaries.getColumnCount() == 0);
}

TEST_CASE("Summaries count the memory their columns hold")
{
    const auto data = makeTrades(1000);
    const auto view = makeView(data);
    Viewer::Summaries summaries;
    CHECK(summaries.getBytes() == 0);

    summaries.update(data, view, makeLayout(20));
    CHECK(summaries.getBytes() >= 20 * sizeof(Core::Data::ColumnSummary));
}
