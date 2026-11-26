#include "EngineTesting.hpp"

#include "Gpu/Market/Style.hpp"

#include "Core/Plot/Sizes.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>

using namespace EngineTesting;

TEST_CASE("Trades are drawn where their time and price fall, colored by side")
{
    const auto engine = createOffscreen(100, 50);
    if (!engine)
    {
        return;
    }
    CHECK(engine->setTrades(buyThenSell()));
    engine->setSettings({ .view = viewOf({ .first = 0, .last = 2 }) });
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels)
    {
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
    if (!engine)
    {
        return;
    }
    CHECK(engine->setTrades(buyThenSell()));
    engine->setSettings({
        // The plot is the top left 80 × 40 pixels.
        .insets = { .left = 0, .top = 0, .right = 20, .bottom = 10 },
        .grid = { .verticalLines = { 0.5f }, .horizontalLines = { 0.25f } },
        .view = viewOf({ .first = 0, .last = 2 }),
    });
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels)
    {
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
    if (!engine)
    {
        return;
    }
    CHECK(engine->setTrades(buyThenSell()));
    engine->setSettings({ .view = viewOf({ .first = 1, .last = 2 }) });
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels)
    {
        return;
    }
    CHECK(isBackground(pixelAt(*pixels, 100, 25, 25)));
    CHECK(isSell(pixelAt(*pixels, 100, 75, 25)));
}

namespace {

using Core::Plot::MarkerScale;
using Gpu::Market::Type::Theme;
namespace Style = Gpu::Market::Style;

const Rgba BackgroundColor = getDefaultBackground();
const Theme DefaultTheme;

/** One trade of `amount`, drawn alone in the middle of a 100 × 100 engine, with `settings` otherwise, its scale aside. */
std::optional<Pixels> drawOne(Engine& engine, double amount, Side side, MarkerScale scale, float pixelRatio, Engine::Settings settings = {})
{
    Trades trades;
    add(trades, NoonUs + 50, 100.0, amount, side);
    CHECK(engine.setTrades(trades));
    settings.view = viewOf({ .first = 0, .last = 1 });
    settings.markerSizing.scale = scale;
    settings.pixelRatio = pixelRatio;
    engine.setSettings(settings);
    engine.render();
    return readPixels(engine);
}

/** A channel of `color` drawn at the markers' opacity over the background. */
double blend(float color, int background, float opacity = DefaultTheme.markerOpacity)
{
    return color * 255.0 * opacity + background * (1.0 - opacity);
}

/** How many pixels of a buy marker's middle row it covers at least half of: its width there, to a pixel. */
int measureDiameter(const Pixels& pixels)
{
    const double halfCovered = (BackgroundColor.green + blend(DefaultTheme.buy.green, BackgroundColor.green)) / 2.0;
    int count = 0;
    for (std::uint32_t x = 0; x < 100; ++x)
    {
        count += pixelAt(pixels, 100, x, 50).green > halfCovered ? 1 : 0;
    }
    return count;
}

}  // namespace

TEST_CASE("Markers are drawn at the legend's sizes, scaled by the pixel ratio")
{
    const auto engine = createOffscreen(100, 100);
    if (!engine)
    {
        return;
    }
    for (const auto scale : { MarkerScale::Area, MarkerScale::Log })
    {
        for (const auto& size : Style::getLegend({ .scale = scale }, DefaultTheme).sizes)
        {
            for (const float pixelRatio : { 1.0f, 2.0f })
            {
                CAPTURE(scale);
                CAPTURE(size.amount);
                CAPTURE(pixelRatio);
                const auto pixels = drawOne(*engine, size.amount, Side::Buy, scale, pixelRatio);
                if (!pixels)
                {
                    return;
                }
                // The middle row's pixel centers lie half a pixel off the marker's center.
                const double radiusPx = size.radiusCssPx * pixelRatio;
                CHECK(std::abs(measureDiameter(*pixels) - 2.0 * std::sqrt(radiusPx * radiusPx - 0.25)) <= 1.0);
            }
        }
    }
}

TEST_CASE("Markers have the legend's colors, at its opacity")
{
    const auto engine = createOffscreen(100, 100);
    if (!engine)
    {
        return;
    }
    const auto legend = Style::getLegend({ .scale = MarkerScale::Area }, DefaultTheme);
    for (const auto side : { Side::Buy, Side::Sell })
    {
        CAPTURE(side == Side::Buy ? "buy" : "sell");
        const auto pixels = drawOne(*engine, 1.0, side, MarkerScale::Area, 1.0f);
        if (!pixels)
        {
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
    if (!engine)
    {
        return;
    }
    // A marker centered at (50, 50), 6 pixels in radius by default. The pixel
    // whose center is in the middle of the ring, and one well past it.
    constexpr double Amount = 9.0 / 64.0;
    const double ringInnerPx = Core::Plot::MarkerSizing{}.getRadiusCssPx(Amount) + Core::Plot::RingGapCssPx;
    const auto ringXPx = static_cast<std::uint32_t>(50.0 + ringInnerPx + (Core::Plot::RingWidthCssPx / 2.0) - 0.5);
    const auto pastRingXPx = static_cast<std::uint32_t>(50.0 + ringInnerPx + Core::Plot::RingWidthCssPx + 2.5);
    const auto plain = drawOne(*engine, Amount, Side::Buy, MarkerScale::Area, 1.0f);
    if (!plain)
    {
        return;
    }
    CHECK(isBackground(pixelAt(*plain, 100, ringXPx, 50)));

    engine->setSettings({ .view = viewOf({ .first = 0, .last = 1 }), .highlightedTrade = 0 });
    engine->render();
    const auto ringed = readPixels(*engine);
    if (!ringed)
    {
        return;
    }
    const Rgba ring = pixelAt(*ringed, 100, ringXPx, 50);
    CHECK(ring.red >= 250);
    CHECK(ring.green >= 250);
    CHECK(ring.blue >= 250);
    CHECK(isBuy(pixelAt(*ringed, 100, 52, 50)));  // the marker itself is unchanged
    CHECK(isBackground(pixelAt(*ringed, 100, pastRingXPx, 50)));

    engine->setSettings({ .view = viewOf({ .first = 0, .last = 1 }) });
    engine->render();
    const auto cleared = readPixels(*engine);
    if (!cleared)
    {
        return;
    }
    CHECK(isBackground(pixelAt(*cleared, 100, ringXPx, 50)));
}

TEST_CASE("Columns are drawn instead of trades, as bars over their price range, colored by side")
{
    const auto engine = createOffscreen(100, 100);
    if (!engine)
    {
        return;
    }
    Trades trades;
    add(trades, NoonUs + 50, 100.0, 1.0, Side::Buy);  // drawn at (50, 50) when trades show
    CHECK(engine->setTrades(trades));
    engine->setSettings({ .view = viewOf({ .first = 0, .last = 1 }) });
    // 10 columns of 10 pixels. Prices 99 to 101 fill the height, top down.
    engine->setColumns(Gpu::Market::Type::Columns{
        .columnCount = 10,
        .columns = {
            { .index = 2, .price = { .low = 99.5, .high = 100.5 }, .buyAmount = 2.0, .sellAmount = 0.0 },  // rows 25 to 75
            { .index = 7, .price = { .low = 100.0, .high = 100.0 }, .buyAmount = 0.0, .sellAmount = 2.0 },  // 2 pixels around row 50
        },
    });
    engine->render();
    const auto pixels = readPixels(*engine);
    if (!pixels)
    {
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

    engine->setSettings({ .view = viewOf({ .first = 0, .last = 1 }), .highlightedColumn = 1 });
    engine->render();
    const auto highlighted = readPixels(*engine);
    if (!highlighted)
    {
        return;
    }
    CHECK(isExactly(pixelAt(*highlighted, 100, 75, 50), DefaultTheme.cursor));
    CHECK(isExactly(pixelAt(*highlighted, 100, 25, 50), DefaultTheme.buy));

    engine->setColumns(std::nullopt);
    engine->render();
    const auto tradesAgain = readPixels(*engine);
    if (!tradesAgain)
    {
        return;
    }
    CHECK(isBuy(pixelAt(*tradesAgain, 100, 50, 50)));
    CHECK(isBackground(pixelAt(*tradesAgain, 100, 25, 30)));
}

TEST_CASE("A theme sets the background, grid and marker colors")
{
    const auto engine = createOffscreen(100, 100);
    if (!engine)
    {
        return;
    }
    const Theme theme{
        .plot = { .background = { .red = 1.0f, .green = 1.0f, .blue = 1.0f }, .grid = { .red = 0.0f, .green = 0.0f, .blue = 1.0f } },
        .buy = { .red = 0.0f, .green = 0.0f, .blue = 0.0f },
        .sell = { .red = 0.0f, .green = 0.0f, .blue = 0.0f },
        .markerOpacity = 1.0f,
    };
    const auto pixels = drawOne(*engine, 1.0, Side::Buy, MarkerScale::Area, 1.0f,
                                { .grid = { .verticalLines = { 0.1f }, .horizontalLines = {} }, .theme = theme });
    if (!pixels)
    {
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
    if (!engine)
    {
        return;
    }
    // 3 µs before the lower 32 bits wrap, so the second trade's subtraction borrows.
    constexpr std::int64_t StartUs = (((NoonUs >> 32) + 1) << 32) - 3;
    // Tiny trades, drawn as small as markers get, 1 µs apart.
    Trades trades;
    add(trades, StartUs + 2, 100.0, 1e-6, Side::Buy);
    add(trades, StartUs + 3, 100.0, 1e-6, Side::Sell);
    CHECK(engine->setTrades(trades));
    engine->setSettings({ .view = { .time = { .startUs = StartUs, .endUs = StartUs + 10 },
                                    .price = { .low = 99.0, .high = 101.0 },
                                    .trades = { .first = 0, .last = 2 } } });
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels)
    {
        return;
    }
    // 10 pixels per microsecond.
    CHECK(isBuy(pixelAt(*pixels, 100, 20, 25)));
    CHECK(isSell(pixelAt(*pixels, 100, 30, 25)));
    CHECK(isBackground(pixelAt(*pixels, 100, 25, 25)));
}
