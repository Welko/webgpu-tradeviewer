#include "Gpu/Market/Engine.hpp"
#include "Gpu/Market/Style.hpp"

#include "Core/Data/Trades.hpp"
#include "Core/Profiling.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {

using Gpu::Market::Engine;
using Pixels = Engine::Pixels;
using Gpu::Type::FrameStats;
using UntimedReason = FrameStats::UntimedReason;
using Core::Data::Side;
using Core::Data::Trades;

constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC

// Built without exceptions: CHECK and return early, instead of REQUIRE.

/** Null, with a failed check, if no engine could be created. */
std::unique_ptr<Engine> createOffscreen(std::uint32_t width, std::uint32_t height)
{
    std::unique_ptr<Engine> engine;
    bool done = false;
    Engine::create(Gpu::Type::Target::Offscreen{ .width = width, .height = height }, Gpu::Type::PowerPreference::Default,
        [&](Engine::CreatedResult result) {
            done = true;
            CHECK_MESSAGE(result, (result ? "" : result.error()));
            if (result) {
                engine = std::move(*result);
            }
        });
    // Natively, creation always finishes before create() returns.
    CHECK_MESSAGE(done, "Engine creation didn't finish before create() returned");
    return engine;
}

/** The offscreen pixels, or nothing, with a failed check, if they don't arrive in 5 s. */
std::optional<Pixels> readPixels(Engine& engine)
{
    std::optional<Pixels> pixels;
    bool done = false;
    engine.readPixels([&](std::expected<Pixels, std::string> result) {
        done = true;
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result) {
            pixels = std::move(*result);
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        engine.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(done, "The readback never completed");
    return pixels;
}

/** Collects every frame's stats as the engine reports them. */
void recordStats(Engine& engine, std::vector<FrameStats>& reported)
{
    engine.setStatsCallback([&reported](const FrameStats& stats) { reported.push_back(stats); });
}

/** Pumps events until `count` frames have been reported. False, with a failed check, if that takes 5 s. */
bool waitForStats(Engine& engine, const std::vector<FrameStats>& reported, std::size_t count)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (reported.size() < count && std::chrono::steady_clock::now() < deadline) {
        engine.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(reported.size() == count, "Not every frame was reported");
    return reported.size() == count;
}

/** Whether every pixel is the engine's background, (0.07, 0.08, 0.10) in 8-bit RGBA. */
bool isBackground(const Pixels& pixels)
{
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (std::abs(pixels[i] - 18) > 1 || std::abs(pixels[i + 1] - 20) > 1 || std::abs(pixels[i + 2] - 26) > 1
            || pixels[i + 3] != 255) {
            return false;
        }
    }
    return !pixels.empty();
}

struct Rgba
{
    int red;
    int green;
    int blue;
    int alpha;
};

/** The pixel at column `x` and row `y`, counting rows from the top. */
Rgba pixelAt(const Pixels& pixels, std::uint32_t width, std::uint32_t x, std::uint32_t y)
{
    const std::size_t i = (std::size_t{ y } * width + x) * 4;
    return { pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3] };
}

bool isBuy(Rgba pixel)
{
    return pixel.green > pixel.red + 50;
}

bool isSell(Rgba pixel)
{
    return pixel.red > pixel.green + 50;
}

bool isBackground(Rgba pixel)
{
    return std::abs(pixel.red - 18) <= 1 && std::abs(pixel.green - 20) <= 1 && std::abs(pixel.blue - 26) <= 1;
}

/** A faint gray grid line over the background. */
bool isGridLine(Rgba pixel)
{
    return pixel.red > 25 && pixel.red < 50 && std::abs(pixel.red - pixel.green) <= 3;
}

/** Adds a trade at `timeUs`. */
void add(Trades& trades, std::int64_t timeUs, double price, double amount, Side side)
{
    trades.timeUs.push_back(timeUs);
    trades.price.push_back(price);
    trades.amount.push_back(amount);
    trades.side.push_back(side);
}

/** A buy a quarter of the way along, mid-height, and a sell three quarters along. */
Trades buyThenSell()
{
    Trades trades;
    add(trades, NoonUs + 25, 100.0, 1.0, Side::Buy);
    add(trades, NoonUs + 75, 100.0, 1.0, Side::Sell);
    return trades;
}

/** 100 µs from noon, prices 99 to 101, showing `trades`. */
Gpu::Market::Type::View viewOf(Core::IndexRange trades)
{
    return { .time = { NoonUs, NoonUs + 100 }, .price = { .low = 99.0, .high = 101.0 }, .trades = trades };
}

} // namespace

TEST_CASE("Trades are drawn where their time and price fall, colored by side")
{
    const auto engine = createOffscreen(100, 50);
    if (!engine) {
        return;
    }
    engine->setTrades(buyThenSell());
    engine->setView(viewOf({ 0, 2 }));
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    CHECK(isBuy(pixelAt(*pixels, 100, 25, 25)));
    CHECK(isSell(pixelAt(*pixels, 100, 75, 25)));
    CHECK(isBackground(pixelAt(*pixels, 100, 50, 25)));
    CHECK(isBackground(pixelAt(*pixels, 100, 25, 5)));
}

TEST_CASE("Insets keep trades and grid lines inside the plot")
{
    const auto engine = createOffscreen(100, 50);
    if (!engine) {
        return;
    }
    // The plot is the top left 80 × 40 pixels.
    engine->setPlotInsets({ .left = 0, .top = 0, .right = 20, .bottom = 10 });
    engine->setTrades(buyThenSell());
    engine->setView(viewOf({ 0, 2 }));
    engine->setGrid({ .verticalLines = { 0.5f }, .horizontalLines = { 0.25f } });
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    // A quarter of the plot across and half of it down.
    CHECK(isBuy(pixelAt(*pixels, 100, 20, 20)));
    CHECK(isSell(pixelAt(*pixels, 100, 60, 20)));
    // One pixel wide, at column 40 and, a quarter up from the plot's bottom, row 29.
    CHECK(isGridLine(pixelAt(*pixels, 100, 40, 5)));
    CHECK(isBackground(pixelAt(*pixels, 100, 39, 5)));
    CHECK(isBackground(pixelAt(*pixels, 100, 41, 5)));
    CHECK(isGridLine(pixelAt(*pixels, 100, 5, 29)));
    CHECK(isBackground(pixelAt(*pixels, 100, 5, 28)));
    // Nothing in the margins.
    CHECK(isBackground(pixelAt(*pixels, 100, 40, 45)));
    CHECK(isBackground(pixelAt(*pixels, 100, 90, 29)));
}

TEST_CASE("Only the view's trades are drawn")
{
    const auto engine = createOffscreen(100, 50);
    if (!engine) {
        return;
    }
    engine->setTrades(buyThenSell());
    engine->setView(viewOf({ 1, 2 }));
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    CHECK(isBackground(pixelAt(*pixels, 100, 25, 25)));
    CHECK(isSell(pixelAt(*pixels, 100, 75, 25)));
}

namespace {

using Gpu::Market::Type::MarkerScale;
using Gpu::Market::Type::Theme;
namespace Style = Gpu::Market::Style;

constexpr Rgba BackgroundColor{ .red = 18, .green = 20, .blue = 26, .alpha = 255 };
const Theme DefaultTheme;

/** One trade of `amount` BTC, drawn alone in the middle of a 100 × 100 engine. */
std::optional<Pixels> drawOne(Engine& engine, double amount, Side side, MarkerScale scale, float pixelRatio)
{
    Trades trades;
    add(trades, NoonUs + 50, 100.0, amount, side);
    engine.setTrades(trades);
    engine.setView(viewOf({ 0, 1 }));
    engine.setMarkerScale(scale);
    engine.setPixelRatio(pixelRatio);
    engine.render();
    return readPixels(engine);
}

/** A channel of `color` drawn at the markers' opacity over the background. */
double blend(float color, int background, float opacity = DefaultTheme.markerOpacity)
{
    return color * 255.0 * opacity + background * (1.0 - opacity);
}

/** How many pixels of a buy marker's middle row it covers at least half of: its diameter, to a pixel. */
int measureDiameter(const Pixels& pixels)
{
    const double halfCovered = (BackgroundColor.green + blend(DefaultTheme.buy.green, BackgroundColor.green)) / 2.0;
    int count = 0;
    for (std::uint32_t x = 0; x < 100; ++x) {
        count += pixelAt(pixels, 100, x, 50).green > halfCovered ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_CASE("Markers are drawn at the legend's sizes, scaled by the pixel ratio")
{
    const auto engine = createOffscreen(100, 100);
    if (!engine) {
        return;
    }
    for (const auto scale : { MarkerScale::Area, MarkerScale::Log }) {
        for (const auto& size : Style::getLegend(scale, DefaultTheme).sizes) {
            for (const float pixelRatio : { 1.0f, 2.0f }) {
                CAPTURE(scale);
                CAPTURE(size.amount);
                CAPTURE(pixelRatio);
                const auto pixels = drawOne(*engine, size.amount, Side::Buy, scale, pixelRatio);
                if (!pixels) {
                    return;
                }
                CHECK(std::abs(measureDiameter(*pixels) - 2.0 * size.radius * pixelRatio) <= 1.0);
            }
        }
    }
}

TEST_CASE("Markers have the legend's colors, at its opacity")
{
    const auto engine = createOffscreen(100, 100);
    if (!engine) {
        return;
    }
    const auto legend = Style::getLegend(MarkerScale::Area, DefaultTheme);
    for (const auto side : { Side::Buy, Side::Sell }) {
        CAPTURE(side == Side::Buy ? "buy" : "sell");
        const auto pixels = drawOne(*engine, 1.0, side, MarkerScale::Area, 1.0f);
        if (!pixels) {
            return;
        }
        const auto color = side == Side::Buy ? legend.buy : legend.sell;
        const Rgba middle = pixelAt(*pixels, 100, 50, 50);
        CHECK(std::abs(middle.red - blend(color.red, BackgroundColor.red)) <= 2.0);
        CHECK(std::abs(middle.green - blend(color.green, BackgroundColor.green)) <= 2.0);
        CHECK(std::abs(middle.blue - blend(color.blue, BackgroundColor.blue)) <= 2.0);
    }
}

TEST_CASE("The highlighted trade gets a ring in the cursor color, just outside its marker")
{
    const auto engine = createOffscreen(100, 100);
    if (!engine) {
        return;
    }
    // A marker of radius 6 centered at (50, 50); the ring runs from 7.5 to 9.5 pixels out.
    const auto plain = drawOne(*engine, 1.0, Side::Buy, MarkerScale::Area, 1.0f);
    if (!plain) {
        return;
    }
    CHECK(isBackground(pixelAt(*plain, 100, 58, 50)));

    engine->setHighlightedTrade(0);
    engine->render();
    const auto ringed = readPixels(*engine);
    if (!ringed) {
        return;
    }
    const Rgba ring = pixelAt(*ringed, 100, 58, 50);
    CHECK(ring.red >= 250);
    CHECK(ring.green >= 250);
    CHECK(ring.blue >= 250);
    CHECK(isBuy(pixelAt(*ringed, 100, 52, 50)));  // the marker itself is unchanged
    CHECK(isBackground(pixelAt(*ringed, 100, 62, 50)));

    engine->setHighlightedTrade(std::nullopt);
    engine->render();
    const auto cleared = readPixels(*engine);
    if (!cleared) {
        return;
    }
    CHECK(isBackground(pixelAt(*cleared, 100, 58, 50)));
}

TEST_CASE("Columns are drawn instead of trades, as bars over their price range, colored by side")
{
    const auto engine = createOffscreen(100, 100);
    if (!engine) {
        return;
    }
    Trades trades;
    add(trades, NoonUs + 50, 100.0, 1.0, Side::Buy);  // drawn at (50, 50) when trades show
    engine->setTrades(trades);
    engine->setView(viewOf({ 0, 1 }));
    // 10 columns of 10 pixels. Prices 99 to 101 fill the height, top down.
    engine->setColumns(Gpu::Market::Type::Columns{
        .across = 10,
        .columns = {
            { .index = 2, .price = { .low = 99.5, .high = 100.5 }, .buyAmount = 2.0, .sellAmount = 0.0 },  // rows 25 to 75
            { .index = 7, .price = { .low = 100.0, .high = 100.0 }, .buyAmount = 0.0, .sellAmount = 2.0 },  // 2 pixels around row 50
        },
    });
    engine->render();
    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    // Each traded as much as the most, so both are opaque.
    const auto isExactly = [](Rgba pixel, Gpu::Type::Color color) {
        return std::abs(pixel.red - color.red * 255.0) <= 1.0 && std::abs(pixel.green - color.green * 255.0) <= 1.0
               && std::abs(pixel.blue - color.blue * 255.0) <= 1.0;
    };
    CHECK(isExactly(pixelAt(*pixels, 100, 25, 50), DefaultTheme.buy));
    CHECK(isExactly(pixelAt(*pixels, 100, 25, 26), DefaultTheme.buy));
    CHECK(isBackground(pixelAt(*pixels, 100, 25, 20)));
    CHECK(isBackground(pixelAt(*pixels, 100, 31, 50)));
    CHECK(isExactly(pixelAt(*pixels, 100, 75, 49), DefaultTheme.sell));
    CHECK(isExactly(pixelAt(*pixels, 100, 75, 50), DefaultTheme.sell));
    CHECK(isBackground(pixelAt(*pixels, 100, 75, 46)));
    CHECK(isBackground(pixelAt(*pixels, 100, 50, 50)));  // the trade isn't drawn

    engine->setHighlightedColumn(1);
    engine->render();
    const auto highlighted = readPixels(*engine);
    if (!highlighted) {
        return;
    }
    CHECK(isExactly(pixelAt(*highlighted, 100, 75, 50), DefaultTheme.cursor));
    CHECK(isExactly(pixelAt(*highlighted, 100, 25, 50), DefaultTheme.buy));

    engine->setColumns(std::nullopt);
    engine->render();
    const auto tradesAgain = readPixels(*engine);
    if (!tradesAgain) {
        return;
    }
    CHECK(isBuy(pixelAt(*tradesAgain, 100, 50, 50)));
    CHECK(isBackground(pixelAt(*tradesAgain, 100, 25, 30)));
}

TEST_CASE("A theme sets the background, grid and marker colors")
{
    const auto engine = createOffscreen(100, 100);
    if (!engine) {
        return;
    }
    const Theme theme{
        .plot = { .background = { .red = 1.0f, .green = 1.0f, .blue = 1.0f }, .grid = { .red = 0.0f, .green = 0.0f, .blue = 1.0f } },
        .buy = { .red = 0.0f, .green = 0.0f, .blue = 0.0f },
        .sell = { .red = 0.0f, .green = 0.0f, .blue = 0.0f },
        .markerOpacity = 1.0f,
    };
    engine->setTheme(theme);
    engine->setGrid({ .verticalLines = { 0.1f }, .horizontalLines = {} });
    const auto pixels = drawOne(*engine, 1.0, Side::Buy, MarkerScale::Area, 1.0f);
    if (!pixels) {
        return;
    }
    const Rgba background = pixelAt(*pixels, 100, 30, 10);
    CHECK(background.red == 255);
    CHECK(background.blue == 255);
    // The line at a tenth of the plot's width is column 10.
    const Rgba line = pixelAt(*pixels, 100, 10, 10);
    CHECK(line.red == 0);
    CHECK(line.blue == 255);
    const Rgba marker = pixelAt(*pixels, 100, 50, 50);
    CHECK(marker.red == 0);
    CHECK(marker.green == 0);
    CHECK(marker.blue == 0);
}

TEST_CASE("Times are exact to the microsecond, decades after 1970")
{
    const auto engine = createOffscreen(100, 50);
    if (!engine) {
        return;
    }
    // 3 µs before the lower 32 bits wrap, so the second trade's subtraction borrows.
    constexpr std::int64_t StartUs = (((NoonUs >> 32) + 1) << 32) - 3;
    // Tiny trades, drawn as small as markers get, 1 µs apart.
    Trades trades;
    add(trades, StartUs + 2, 100.0, 1e-6, Side::Buy);
    add(trades, StartUs + 3, 100.0, 1e-6, Side::Sell);
    engine->setTrades(trades);
    engine->setView({ .time = { StartUs, StartUs + 10 }, .price = { .low = 99.0, .high = 101.0 }, .trades = { 0, 2 } });
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    // 10 pixels per microsecond.
    CHECK(isBuy(pixelAt(*pixels, 100, 20, 25)));
    CHECK(isSell(pixelAt(*pixels, 100, 30, 25)));
    CHECK(isBackground(pixelAt(*pixels, 100, 25, 25)));
}

TEST_CASE("Appending past the reserved room keeps the earlier trades")
{
    const auto engine = createOffscreen(100, 50);
    if (!engine) {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    const auto both = buyThenSell();
    Trades first;
    add(first, both.timeUs[0], both.price[0], both.amount[0], both.side[0]);
    Trades second;
    add(second, both.timeUs[1], both.price[1], both.amount[1], both.side[1]);

    engine->reserveTrades(1);
    engine->appendTrades(first);
    engine->render();
    engine->appendTrades(second);
    engine->setView(viewOf({ 0, 2 }));
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels || reported.size() != 2) {
        CHECK(reported.size() == 2);
        return;
    }
    CHECK(isBuy(pixelAt(*pixels, 100, 25, 25)));
    CHECK(isSell(pixelAt(*pixels, 100, 75, 25)));
    // The bigger buffer replaced the first: as many buffers, more bytes.
    CHECK(reported[1].buffers == reported[0].buffers);
    CHECK(reported[1].gpuBytes > reported[0].gpuBytes);
}

TEST_CASE("Zooming and panning allocate nothing")
{
    const auto engine = createOffscreen(100, 50);
    if (!engine) {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    engine->setTrades(buyThenSell());
    for (std::int64_t step = 0; step < 50; ++step) {
        engine->setView({ .time = { NoonUs + step, NoonUs + 100 + step * 3 }, .price = { .low = 99.0, .high = 101.0 },
                          .trades = { 0, 2 } });
        engine->render();
    }
    CHECK(reported.size() == 50);
    for (const auto& stats : reported) {
        CHECK(stats.buffers == reported.front().buffers);
        CHECK(stats.textures == reported.front().textures);
        CHECK(stats.gpuBytes == reported.front().gpuBytes);
    }
}

TEST_CASE("An offscreen engine draws its background into every pixel")
{
    const auto engine = createOffscreen(64, 16);
    if (!engine) {
        return;
    }
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    CHECK(pixels->size() == 64 * 16 * 4);
    CHECK(isBackground(*pixels));
}

TEST_CASE("Resizing an offscreen engine replaces its texture")
{
    const auto engine = createOffscreen(64, 16);
    if (!engine) {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    engine->render();
    engine->resize(32, 8);
    CHECK(engine->needsRender());
    engine->render();
    CHECK_FALSE(engine->needsRender());

    const auto pixels = readPixels(*engine);
    if (!pixels) {
        return;
    }
    CHECK(pixels->size() == 32 * 8 * 4);
    CHECK(isBackground(*pixels));
    CHECK(reported.size() == 2);
    if (reported.size() == 2) {
        CHECK(reported[1].textures == 1);
        CHECK(reported[0].gpuBytes - reported[1].gpuBytes == (64 * 16 - 32 * 8) * 4);
    }
}

TEST_CASE("Profiling reports CPU times and the GPU time of each pass")
{
    if constexpr (!Core::Profiling::Compiled) {
        return;
    }
    const auto engine = createOffscreen(64, 16);
    if (!engine) {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    engine->setProfiling(true);
    engine->render();
    if (!waitForStats(*engine, reported, 1)) {
        return;
    }

    const auto& stats = reported[0];
    CHECK(stats.cpu);
    // CI relies on this: it fails, rather than skips, on a device without
    // timestamp-query.
    CHECK_MESSAGE(stats.gpu, "No GPU timings arrived; the device needs timestamp-query");
    if (stats.gpu) {
        CHECK(stats.gpu->passes.size() == 2);
        if (stats.gpu->passes.size() == 2) {
            CHECK(stats.gpu->passes[0].name == "grid");
            CHECK(stats.gpu->passes[1].name == "trades");
        }
    }
}

TEST_CASE("Each frame is reported once, in order, with times only if it was profiled")
{
    const auto engine = createOffscreen(16, 16);
    if (!engine) {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);

    engine->render();
    engine->setProfiling(true);
    engine->render();
    engine->setProfiling(false);
    // Untimed, but still reported after frame 2, whose GPU times are on their way.
    engine->render();
    if (!waitForStats(*engine, reported, 3)) {
        return;
    }
    for (std::size_t i = 0; i < reported.size(); ++i) {
        CHECK(reported[i].frame == i + 1);
    }
    CHECK_FALSE(reported[0].cpu);
    CHECK(reported[0].gpu.error_or({}) == UntimedReason::ProfilingOff);
    CHECK(reported[1].cpu.has_value() == Core::Profiling::Compiled);
    CHECK(reported[1].gpu.has_value() == Core::Profiling::Compiled);
    CHECK_FALSE(reported[2].cpu);
    CHECK(reported[2].gpu.error_or({}) == UntimedReason::ProfilingOff);
}

TEST_CASE("An offscreen target needs a size")
{
    std::optional<Engine::CreatedResult> result;
    Engine::create(Gpu::Type::Target::Offscreen{ .width = 0, .height = 16 }, Gpu::Type::PowerPreference::Default,
        [&](Engine::CreatedResult created) { result = std::move(created); });
    CHECK(result);
    if (result) {
        CHECK_FALSE(*result);
    }
}
