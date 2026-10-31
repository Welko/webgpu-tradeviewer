#include "Viewer/App.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <memory>

namespace {

// One column per pixel at a pixel ratio of 1, with no insets.
constexpr std::uint32_t Width = 400;
constexpr std::uint32_t Height = 200;

/** An app drawing offscreen, showing the synthetic day; null, with a failed check, if it can't start. */
std::unique_ptr<Viewer::App> createApp()
{
    std::unique_ptr<Viewer::App> app;
    Viewer::App::create(Gpu::Type::Target::Offscreen{ .width = Width, .height = Height }, Gpu::Type::PowerPreference::Default,
        [&](Viewer::App::Result result) {
            CHECK_MESSAGE(result, (result ? "" : result.error()));
            if (result) {
                app = std::move(*result);
            }
        });
    if (app) {
        app->resize(Width, Height);
        app->setTrades(Viewer::generateSyntheticDay());
    }
    return app;
}

} // namespace

TEST_CASE("A view with more than 32 trades per pixel column on average shows column summaries")
{
    const auto app = createApp();
    if (!app) {
        return;
    }
    // The hour holds about 17,900 synthetic trades: 45 per column.
    CHECK(app->getViewStats().count > 32 * Width);
    CHECK(app->getColumnsAcross() == Width);

    // Every trade in view is in exactly one column.
    std::size_t count = 0;
    for (const auto& column : app->getColumns()) {
        count += column.trades.size();
    }
    CHECK(count == app->getViewStats().count);

    // A minute holds about 300.
    app->zoomAt(Width / 2.0, 1.0 / 60.0);
    CHECK(app->getViewStats().count < 32 * Width);
    CHECK(app->getColumnsAcross() == 0);
    CHECK(app->getColumns().empty());
}

TEST_CASE("The threshold can be set, and summaries turned off")
{
    const auto app = createApp();
    if (!app) {
        return;
    }
    app->setSummaryThreshold(1000.0);
    CHECK(app->getColumnsAcross() == 0);
    app->showDay();
    CHECK(app->getColumnsAcross() == Width);  // over 1000 per column

    app->setSummariesAllowed(false);
    CHECK(app->getColumnsAcross() == 0);
    app->setSummariesAllowed(true);
    CHECK(app->getColumnsAcross() == Width);
}

TEST_CASE("While summaries show, the pointer picks columns, not trades")
{
    const auto app = createApp();
    if (!app) {
        return;
    }
    app->showDay();
    const auto& columns = app->getColumns();
    CHECK(columns.size() > 100);
    if (columns.size() <= 100) {
        return;
    }
    // Down the middle of column 100 until its bar, somewhere up the plot, is within reach.
    const double x = static_cast<double>(columns[100].index) + 0.5;
    double y = 0.0;
    for (; y < Height && !app->getPickedColumn(); y += 4.0) {
        app->setPointer(x, y);
    }
    const auto picked = app->getPickedColumn();
    CHECK(!app->getPickedTrade());
    CHECK(picked.has_value());
    if (picked) {
        // The nearest bar, within 24 pixels.
        CHECK(std::abs(picked->centerX - x) <= 24.0);
        CHECK(picked->centerX == doctest::Approx(static_cast<double>(picked->summary.index) + 0.5));
        CHECK(picked->time.startUs <= app->getTrades().timeUs[picked->summary.trades.first]);
        CHECK(app->getTrades().timeUs[picked->summary.trades.last - 1] < picked->time.endUs);
    }

    app->clearPointer();
    CHECK(!app->getPickedColumn());
}
