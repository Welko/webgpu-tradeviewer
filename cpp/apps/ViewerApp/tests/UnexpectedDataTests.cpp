// Odd but valid trades, as a file can hold them: the viewer must draw them,
// answer questions about them and stay usable, whatever they are.

#include "Viewer/App.hpp"

#include "Core/Data/TradeParser.hpp"
#include "Gpu/Market/Style.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t WidthPx = 400;
constexpr std::uint32_t HeightPx = 200;
constexpr std::int64_t DayStartUs = 1'575'158'400'000'000;  // 2019-12-01 UTC

/** A CSV of the given rows: time, side, price, amount. */
std::string csv(std::string_view rows)
{
    return std::format("timestamp,side,price,amount\n{}", rows);
}

/** `count` trades, one microsecond apart from `startUs`, with prices and amounts by position. */
std::string many(std::size_t count, std::int64_t startUs, double (*price)(std::size_t), double (*amount)(std::size_t))
{
    std::string rows;
    for (std::size_t i = 0; i < count; ++i) {
        rows += std::format("{},{},{},{}\n", startUs + static_cast<std::int64_t>(i), i % 2 == 0 ? "buy" : "sell", price(i), amount(i));
    }
    return csv(rows);
}

struct Case
{
    const char* name;
    std::string file;
};

std::vector<Case> getCases()
{
    const auto noon = std::to_string(DayStartUs + 43'200'000'000);
    const auto same = [](std::size_t) { return 7500.0; };
    const auto one = [](std::size_t) { return 1.0; };
    const auto varied = [](std::size_t i) { return 7500.0 + static_cast<double>(i % 100); };
    return {
        { .name = "one trade", .file = csv(noon + ",buy,7500,1\n") },
        { .name = "two trades at the same moment", .file = csv(noon + ",buy,7500,1\n" + noon + ",sell,7501,2\n") },
        { .name = "the same price throughout", .file = many(1000, DayStartUs, same, [](std::size_t i) { return 0.01 + static_cast<double>(i); }) },
        { .name = "the same amount throughout", .file = many(1000, DayStartUs, varied, one) },
        { .name = "the same everything", .file = many(1000, DayStartUs, same, one) },
        { .name = "thousands in one moment", .file = csv([&] {
              std::string rows;
              for (int i = 0; i < 20'000; ++i) {
                  rows += std::format("{},buy,{},1\n", noon, 7500 + i % 10);
              }
              return rows;
          }()) },
        { .name = "enormous prices", .file = csv(noon + ",buy,1e18,1\n" + noon + ",sell,9e17,1\n") },
        { .name = "tiny prices", .file = csv(noon + ",buy,1e-18,1\n" + noon + ",sell,2e-18,1\n") },
        { .name = "prices from tiny to enormous", .file = csv(noon + ",buy,1e-18,1\n" + noon + ",sell,1e18,1\n") },
        { .name = "enormous amounts", .file = csv(noon + ",buy,7500,1e15\n" + noon + ",sell,7500,1e16\n") },
        { .name = "tiny amounts", .file = csv(noon + ",buy,7500,1e-300\n" + noon + ",sell,7500,1e-290\n") },
        { .name = "amounts from tiny to enormous", .file = csv(noon + ",buy,7500,1e-300\n" + noon + ",sell,7500,1e15\n") },
        { .name = "before 1970", .file = csv("-1000000000,buy,7500,1\n-999999999,sell,7501,1\n") },
        { .name = "the last moment a browser can show", .file = csv("8640000000000000000,buy,7500,1\n") },
        { .name = "the first moment a browser can show", .file = csv("-8640000000000000000,buy,7500,1\n") },
        { .name = "two trades centuries apart", .file = csv("0,buy,7500,1\n4102444800000000,sell,7501,1\n") },
    };
}

std::unique_ptr<Viewer::App> createApp()
{
    std::unique_ptr<Viewer::App> app;
    Viewer::App::create({ .target = Gpu::Type::Target::Offscreen{ .width = WidthPx, .height = HeightPx } },
        [&](Viewer::App::Result result) {
            CHECK_MESSAGE(result, (result ? "" : result.error()));
            if (result) {
                app = std::move(*result);
            }
        });
    if (app) {
        app->resize(WidthPx, HeightPx);
    }
    return app;
}

/** Pixels in a marker's or bar's colors: green or red, not the grid's gray. */
std::size_t countColored(Viewer::App& app)
{
    app.render();
    std::vector<std::uint8_t> pixels;
    bool done = false;
    app.readPixels([&](std::expected<std::vector<std::uint8_t>, std::string> result) {
        done = true;
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result) {
            pixels = std::move(*result);
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        app.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(done, "The readback never completed");
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        count += std::abs(pixels[i] - pixels[i + 1]) > 20 ? 1 : 0;
    }
    return count;
}

/** Everything the viewer shows about the view is a real number, and sensible. */
void checkShown(const Viewer::App& app)
{
    for (const auto* ticks : { &app.getAxes().times, &app.getAxes().prices }) {
        CHECK(ticks->size() < 100);
        for (const auto& tick : *ticks) {
            CHECK(std::isfinite(tick.fraction));
            CHECK_FALSE(tick.label.empty());
        }
    }
    const auto legend = app.getLegend();
    CHECK(legend.sizes.size() >= 3);
    CHECK(legend.sizes.size() <= 6);
    double last = 0.0;
    for (const auto& size : legend.sizes) {
        CAPTURE(size.amount);
        CHECK(std::isfinite(size.amount));
        CHECK(size.amount > last);
        CHECK(size.radiusCssPx >= Gpu::Market::Style::MarkerMinRadiusCssPx);
        CHECK(size.radiusCssPx <= Gpu::Market::Style::MarkerMaxRadiusCssPx);
        last = size.amount;
    }
    const auto stats = app.getViewStats();
    CHECK(std::isfinite(stats.totalAmount));
    CHECK(stats.totalAmount >= 0.0);
    if (const auto picked = app.getPickedTrade()) {
        CHECK(std::isfinite(picked->centerXPx));
        CHECK(std::isfinite(picked->centerYPx));
    }
    if (const auto picked = app.getPickedColumn()) {
        CHECK(std::isfinite(picked->topPx));
        CHECK(std::isfinite(picked->bottomPx));
    }
}

/** Draws what's on show, and points at it, checking what comes out. */
void look(Viewer::App& app)
{
    checkShown(app);
    for (const double xPx : { 0.0, WidthPx / 2.0, WidthPx - 1.0 }) {
        for (const double yPx : { 0.0, HeightPx / 2.0, HeightPx - 1.0 }) {
            app.setPointer(xPx, yPx);
            checkShown(app);
        }
    }
    app.clearPointer();
    app.render();
}

} // namespace

TEST_CASE("Odd but valid trades draw, and can be zoomed, panned and pointed at")
{
    const auto app = createApp();
    if (!app) {
        return;
    }
    for (const auto& oddCase : getCases()) {
        CAPTURE(oddCase.name);
        Core::Data::TradeParser parser;
        const auto appended = parser.append(oddCase.file);
        CHECK_MESSAGE(appended, (appended ? "" : appended.error()));
        auto trades = parser.finish();
        CHECK_MESSAGE(trades, (trades ? "" : trades.error()));
        if (!trades) {
            continue;
        }
        const auto set = app->setTrades(std::move(*trades));
        CHECK_MESSAGE(set, (set ? "" : set.error()));

        for (const auto scale : { Gpu::Market::Type::MarkerScale::Area, Gpu::Market::Type::MarkerScale::Log }) {
            CAPTURE(scale);
            app->setMarkerScale(scale);
            app->setSummariesAllowed(true);
            // The whole day of the first trade: it must show, as a marker or a bar.
            app->showDay();
            look(*app);
            CHECK(countColored(*app) > 0);
            app->setSummariesAllowed(false);
            look(*app);
            CHECK(countColored(*app) > 0);
            app->setSummariesAllowed(true);

            // As far in and out as zooming goes, then panning away and back.
            for (int i = 0; i < 40; ++i) {
                app->zoomAt(WidthPx / 2.0, 0.5);
            }
            look(*app);
            for (int i = 0; i < 80; ++i) {
                app->zoomAt(WidthPx / 2.0, 2.0);
            }
            look(*app);
            for (int i = 0; i < 100; ++i) {
                app->panBy(WidthPx);
            }
            look(*app);
            for (int i = 0; i < 200; ++i) {
                app->panBy(-static_cast<double>(WidthPx));
            }
            look(*app);
            app->resetView();
            look(*app);
        }
        const auto health = app->getGpuHealth();
        for (const auto& error : health.errors) {
            FAIL_CHECK(error);
        }
        CHECK(health.errorCount == 0);
    }
}

TEST_CASE("A viewer with no trades yet stays usable")
{
    const auto app = createApp();
    if (!app) {
        return;
    }
    // No trades yet.
    look(*app);
    app->showDay();
    app->zoomAt(WidthPx / 2.0, 0.5);
    app->panBy(10.0);
    look(*app);
    CHECK(app->getViewStats().count == 0);
    CHECK(app->getInstrument().base == "base");
}
