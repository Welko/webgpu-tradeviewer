#include "Viewer/App.hpp"

#include "Core/Memory.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>

namespace {

// One column per pixel at a pixel ratio of 1, with no insets.
constexpr std::uint32_t WidthPx = 400;
constexpr std::uint32_t HeightPx = 200;

/**
 * An app drawing offscreen, holding at most `maxTrades` trades if given,
 * showing the synthetic day; null, with a failed check, if it can't start.
 */
std::unique_ptr<Viewer::App> createApp(std::optional<std::size_t> maxTrades = std::nullopt)
{
    std::unique_ptr<Viewer::App> app;
    Viewer::App::create({ .target = Gpu::Type::Target::Offscreen{ .width = WidthPx, .height = HeightPx }, .maxTrades = maxTrades },
        [&](Viewer::App::Result result) {
            CHECK_MESSAGE(result, (result ? "" : result.error()));
            if (result) {
                app = std::move(*result);
            }
        });
    if (app) {
        app->resize(WidthPx, HeightPx);
        CHECK(app->setTrades(Viewer::generateSyntheticDay()));
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
    CHECK(app->getViewStats().count > 32 * WidthPx);
    CHECK(app->getColumnsAcross() == WidthPx);

    // Every trade in view is in exactly one column.
    std::size_t count = 0;
    for (const auto& column : app->getColumns()) {
        count += column.trades.size();
    }
    CHECK(count == app->getViewStats().count);

    // A minute holds about 300.
    app->zoomAt(WidthPx / 2.0, 1.0 / 60.0);
    CHECK(app->getViewStats().count < 32 * WidthPx);
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
    CHECK(app->getColumnsAcross() == WidthPx);  // over 1000 per column

    app->setSummariesAllowed(false);
    CHECK(app->getColumnsAcross() == 0);
    app->setSummariesAllowed(true);
    CHECK(app->getColumnsAcross() == WidthPx);
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
    const double xPx = static_cast<double>(columns[100].index) + 0.5;
    double yPx = 0.0;
    for (; yPx < HeightPx && !app->getPickedColumn(); yPx += 4.0) {
        app->setPointer(xPx, yPx);
    }
    const auto picked = app->getPickedColumn();
    CHECK(!app->getPickedTrade());
    CHECK(picked.has_value());
    if (picked) {
        // The nearest bar, within 24 pixels.
        CHECK(std::abs(picked->centerXPx - xPx) <= 24.0);
        CHECK(picked->centerXPx == doctest::Approx(static_cast<double>(picked->summary.index) + 0.5));
        CHECK(picked->time.startUs <= app->getTrades().timeUs[picked->summary.trades.first]);
        CHECK(app->getTrades().timeUs[picked->summary.trades.last - 1] < picked->time.endUs);
    }

    app->clearPointer();
    CHECK(!app->getPickedColumn());
}

TEST_CASE("Each trade takes about 47 bytes of memory, and 20 on the GPU")
{
    auto app = createApp();
    if (!app) {
        return;
    }
    const std::size_t count = app->getTrades().size();
    const auto memory = app->getMemoryStats();
    CHECK(memory.tradesBytes == 25 * count);
    CHECK(memory.totalsBytes == 20 * (count + 1));  // one more, for none
    // 32 per block of 16 trades: 2 per trade.
    CHECK(memory.priceTreeBytes == 32 * ((count + 15) / 16));
    CHECK(memory.gpuBytes >= 20 * count);
    // The trades are nearly all of it.
    CHECK(memory.gpuBytes < 20 * count + (1u << 20));
    if (const auto process = Core::Memory::getProcessBytes()) {
        CHECK(memory.getCpuBytes() < *process);
    }
}

TEST_CASE("Zooming, panning and resizing leave memory as it was")
{
    auto app = createApp();
    if (!app) {
        return;
    }
    // From the day to an hour and back, dragged about, at two sizes.
    const auto cycle = [&] {
        app->showDay();
        app->render();
        for (int i = 0; i < 24; ++i) {
            app->zoomAt(WidthPx / 3.0, 0.8);
            app->panBy(7.0);
            app->render();
        }
        app->resize(WidthPx / 2, HeightPx);
        app->render();
        app->resize(WidthPx, HeightPx);
        app->resetView();
        app->render();
    };
    cycle();
    const auto before = app->getMemoryStats();
    for (int i = 0; i < 10; ++i) {
        cycle();
    }
    const auto after = app->getMemoryStats();
    CHECK(after.getCpuBytes() == before.getCpuBytes());
    CHECK(after.summariesBytes == before.summariesBytes);
    CHECK(after.gpuBytes == before.gpuBytes);
}

TEST_CASE("Trades too many for the GPU are refused, and those on show stay")
{
    // Room for the synthetic day, and no more.
    const std::size_t shown = Viewer::generateSyntheticDay().size();
    auto app = createApp(shown);
    if (!app) {
        return;
    }
    auto more = Viewer::generateSyntheticDay();
    more.timeUs.push_back(more.timeUs.back());
    more.price.push_back(more.price.back());
    more.amount.push_back(more.amount.back());
    more.side.push_back(more.side.back());
    const auto set = app->setTrades(std::move(more));
    CHECK_FALSE(set);
    if (!set) {
        CHECK(set.error().find("trades are too many: at most") != std::string::npos);
    }
    CHECK(app->getTrades().size() == shown);
    CHECK(app->getViewStats().count > 0);
}
