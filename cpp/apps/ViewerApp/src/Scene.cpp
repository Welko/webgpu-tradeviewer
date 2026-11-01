#include "Viewer/Scene.hpp"

#include "Core/Data/Trades.hpp"
#include "Gpu/Market/Type.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

namespace Viewer {

namespace {

constexpr std::int64_t DayStartUs = 1'575'158'400'000'000;  // 2019-12-01 UTC
constexpr std::int64_t MinuteUs = 60'000'000;

using Rgb = std::array<std::uint8_t, 3>;

Gpu::Type::Color toColor(Rgb rgb)
{
    return { .red = rgb[0] / 255.0f, .green = rgb[1] / 255.0f, .blue = rgb[2] / 255.0f };
}

std::array<std::uint8_t, 4> opaque(Rgb rgb)
{
    return { rgb[0], rgb[1], rgb[2], 255 };
}

/** `color` at `opacity` over `background`, as the blend state computes it, rounded. */
std::array<std::uint8_t, 4> over(Rgb color, Rgb background, float opacity)
{
    std::array<std::uint8_t, 4> result{ 0, 0, 0, 255 };
    for (std::size_t channel = 0; channel < 3; ++channel) {
        result[channel] = static_cast<std::uint8_t>(std::lround(color[channel] * opacity + background[channel] * (1.0f - opacity)));
    }
    return result;
}

// The app's look: synthetic trades in the default colors.

// Room for the axis labels, as the web app leaves it at a pixel ratio of 1.
constexpr Gpu::Plot::Type::Insets AppInsets{ .left = 0, .top = 0, .right = 64, .bottom = 22 };
constexpr std::uint32_t AppWidthPx = 960;
constexpr std::uint32_t AppHeightPx = 540;

/** The default background, in a corner left for axis labels. 0.1 × 255 is 25.5, so ±1. */
Scene::Patch appBackground()
{
    const auto background = Gpu::Market::Type::Theme{}.plot.background;
    const auto byte = [](float channel) { return static_cast<std::uint8_t>(std::lround(channel * 255.0f)); };
    return { .what = "background, under the axis labels",
             .xPx = AppWidthPx - 4,
             .yPx = AppHeightPx - 4,
             .rgba = { byte(background.red), byte(background.green), byte(background.blue), 255 },
             .tolerance = 1 };
}

/** Moves the pointer down the middle of the plot until it picks a trade or a column. */
void pointAtMiddle(App& app)
{
    const double xPx = (AppWidthPx - AppInsets.right) / 2.0;
    for (std::uint32_t yPx = 0; yPx < AppHeightPx - AppInsets.bottom; yPx += 4) {
        app.setPointer(xPx, yPx);
        if (app.getPickedTrade() || app.getPickedColumn()) {
            return;
        }
    }
}

void setUpHour(App& app)
{
    app.setTrades(generateSyntheticDay());
    pointAtMiddle(app);
}

void setUpDay(App& app)
{
    app.setTrades(generateSyntheticDay());
    app.showDay();
    pointAtMiddle(app);
}

// Colors in whole 255ths, so flat ones come out exact. Grid lines in the
// background color, so they don't show under the markers.

constexpr Rgb Background{ 32, 36, 44 };
constexpr Rgb Buy{ 40, 200, 120 };
constexpr Rgb Sell{ 230, 70, 60 };
constexpr Rgb Cursor{ 250, 230, 20 };
constexpr float BlendedOpacity = 0.5f;

// The plot is the top left 360 × 180 pixels. A buy at 12:15 and a sell at
// 12:45 of the 12:00–13:00 view, both at mid-height, so their centers are at
// (90, 90) and (270, 90). Both are large enough for the largest marker, 16
// pixels across.
constexpr Gpu::Plot::Type::Insets ColorInsets{ .left = 0, .top = 0, .right = 40, .bottom = 20 };
constexpr std::uint32_t ColorWidthPx = 400;
constexpr std::uint32_t ColorHeightPx = 200;
constexpr std::uint32_t BuyXPx = 90;
constexpr std::uint32_t SellXPx = 270;
constexpr std::uint32_t MarkersYPx = 90;
// The ring is 2 pixels wide, 1.5 pixels outside the marker's 16: fully
// covered 18.5 pixels from the center.
constexpr std::uint32_t RingYPx = MarkersYPx - 19;

void setUpColors(App& app, float opacity)
{
    Core::Data::Trades trades;
    const auto add = [&](std::int64_t timeUs, Core::Data::Side side) {
        trades.timeUs.push_back(timeUs);
        trades.price.push_back(100.0);
        trades.amount.push_back(100.0);
        trades.side.push_back(side);
    };
    add(DayStartUs + 12 * 60 * MinuteUs + 15 * MinuteUs, Core::Data::Side::Buy);
    add(DayStartUs + 12 * 60 * MinuteUs + 45 * MinuteUs, Core::Data::Side::Sell);
    app.setTrades(std::move(trades));
    app.setTheme({
        .plot = { .background = toColor(Background), .grid = toColor(Background) },
        .buy = toColor(Buy),
        .sell = toColor(Sell),
        .markerOpacity = opacity,
        .cursor = toColor(Cursor),
    });
}

const std::vector<Scene>& getAll()
{
    static const std::vector<Scene> scenes{
        {
            .name = "hour",
            .description = "Synthetic trades from 12:00 to 13:00 as markers, one in the middle ringed",
            .widthPx = AppWidthPx,
            .heightPx = AppHeightPx,
            .insets = AppInsets,
            .patches = { appBackground() },
            .setUp = setUpHour,
        },
        {
            .name = "day",
            .description = "The whole synthetic day as column summaries, the one in the middle highlighted",
            .widthPx = AppWidthPx,
            .heightPx = AppHeightPx,
            .insets = AppInsets,
            .patches = { appBackground() },
            .setUp = setUpDay,
        },
        {
            .name = "opaque",
            .description = "A buy and a sell in opaque colors chosen for the test, the buy ringed",
            .widthPx = ColorWidthPx,
            .heightPx = ColorHeightPx,
            .insets = ColorInsets,
            .patches = {
                { .what = "background", .xPx = ColorWidthPx - 4, .yPx = ColorHeightPx - 4, .rgba = opaque(Background), .tolerance = 0 },
                { .what = "buy marker", .xPx = BuyXPx, .yPx = MarkersYPx, .rgba = opaque(Buy), .tolerance = 0 },
                { .what = "sell marker", .xPx = SellXPx, .yPx = MarkersYPx, .rgba = opaque(Sell), .tolerance = 0 },
                { .what = "ring", .xPx = BuyXPx, .yPx = RingYPx, .rgba = opaque(Cursor), .tolerance = 0 },
                { .what = "between the markers", .xPx = (BuyXPx + SellXPx) / 2, .yPx = MarkersYPx, .rgba = opaque(Background), .tolerance = 0 },
            },
            .setUp = [](App& app) {
                setUpColors(app, 1.0f);
                app.setPointer(BuyXPx, MarkersYPx);
            },
        },
        {
            .name = "blended",
            .description = "The same buy and sell at half opacity over the background",
            .widthPx = ColorWidthPx,
            .heightPx = ColorHeightPx,
            .insets = ColorInsets,
            .patches = {
                { .what = "background", .xPx = ColorWidthPx - 4, .yPx = ColorHeightPx - 4, .rgba = opaque(Background), .tolerance = 0 },
                { .what = "buy marker", .xPx = BuyXPx, .yPx = MarkersYPx, .rgba = over(Buy, Background, BlendedOpacity), .tolerance = 1 },
                { .what = "sell marker", .xPx = SellXPx, .yPx = MarkersYPx, .rgba = over(Sell, Background, BlendedOpacity), .tolerance = 1 },
            },
            .setUp = [](App& app) { setUpColors(app, BlendedOpacity); },
        },
    };
    return scenes;
}

} // namespace

std::span<const Scene> Scene::getScenes()
{
    return getAll();
}

const Scene* Scene::find(std::string_view name)
{
    const auto& scenes = getAll();
    const auto found = std::ranges::find(scenes, name, &Scene::name);
    return found != scenes.end() ? &*found : nullptr;
}

void Scene::show(const Scene& scene, App& app)
{
    app.resize(scene.widthPx, scene.heightPx);
    app.setPixelRatio(1.0f);
    app.setPlotInsets(scene.insets);
    app.setSummariesAllowed(true);
    app.setMarkerScale(Gpu::Market::Type::MarkerScale::Area);
    app.setTheme({});
    app.clearPointer();
    scene.setUp(app);
}

std::vector<std::string> Scene::checkPatches(const Scene& scene, std::span<const std::uint8_t> rgba)
{
    if (rgba.size() != std::size_t{ scene.widthPx } * scene.heightPx * 4) {
        return { std::format("{} bytes, not {} × {} pixels", rgba.size(), scene.widthPx, scene.heightPx) };
    }
    std::vector<std::string> failures;
    for (const auto& patch : scene.patches) {
        const std::size_t i = (std::size_t{ patch.yPx } * scene.widthPx + patch.xPx) * 4;
        const bool close = std::ranges::all_of(std::array{ 0, 1, 2, 3 }, [&](std::size_t channel) {
            return std::abs(rgba[i + channel] - patch.rgba[channel]) <= patch.tolerance;
        });
        if (!close) {
            failures.push_back(std::format("{} at ({}, {}): expected ({}, {}, {}, {}) ±{}, got ({}, {}, {}, {})", patch.what,
                                           patch.xPx, patch.yPx, patch.rgba[0], patch.rgba[1], patch.rgba[2], patch.rgba[3],
                                           patch.tolerance, rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]));
        }
    }
    return failures;
}

} // namespace Viewer
