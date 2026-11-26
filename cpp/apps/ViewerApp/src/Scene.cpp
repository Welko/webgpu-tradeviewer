#include "Viewer/Scene.hpp"

#include "Core/Data/Trades.hpp"
#include "Core/Log.hpp"
#include "Core/Plot/Sizes.hpp"
#include "Gpu/Market/Type.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iterator>

namespace Viewer {

namespace {

using Core::HourUs;
using Core::MinuteUs;

constexpr std::int64_t DayStartUs = 1'575'158'400'000'000;  // 2019-12-01 UTC

using Rgb = std::array<std::uint8_t, 3>;

Gpu::Type::Color toColor(Rgb rgb)
{
    const auto channel = [](std::uint8_t value) { return static_cast<float>(value) / 255.0f; };
    return { .red = channel(rgb[0]), .green = channel(rgb[1]), .blue = channel(rgb[2]) };
}

std::array<std::uint8_t, 4> opaque(Rgb rgb)
{
    return { rgb[0], rgb[1], rgb[2], 255 };
}

/** `color` at `opacity` over `background`, as the blend state computes it, rounded. */
std::array<std::uint8_t, 4> over(Rgb color, Rgb background, float opacity)
{
    std::array<std::uint8_t, 4> result{ 0, 0, 0, 255 };
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        const float blended = static_cast<float>(color[channel]) * opacity + static_cast<float>(background[channel]) * (1.0f - opacity);
        result[channel] = static_cast<std::uint8_t>(std::lround(blended));
    }
    return result;
}

/**
 * The margins left for axis labels, right and below the plot, which nothing
 * is drawn in: all background.
 */
std::vector<Scene::Patch> margins(std::uint32_t widthPx, std::uint32_t heightPx, Core::Plot::Insets insets,
                                  std::array<std::uint8_t, 4> background, int tolerance)
{
    return {
        { .what = "right margin",
          .xPx = widthPx - insets.right,
          .yPx = 0,
          .widthPx = insets.right,
          .heightPx = heightPx,
          .rgba = background,
          .tolerance = tolerance },
        { .what = "bottom margin",
          .xPx = 0,
          .yPx = heightPx - insets.bottom,
          .widthPx = widthPx - insets.right,
          .heightPx = insets.bottom,
          .rgba = background,
          .tolerance = tolerance },
    };
}

/** A default theme color in whole 255ths. Some fall halfway, such as 0.1 × 255 = 25.5, so they're checked ±1. */
std::array<std::uint8_t, 4> toBytes(Gpu::Type::Color color)
{
    const auto byte = [](float channel) { return static_cast<std::uint8_t>(std::lround(channel * 255.0f)); };
    return { byte(color.red), byte(color.green), byte(color.blue), 255 };
}

// The app's look: synthetic trades in the default colors.

// Room for the axis labels, as the web app leaves it at a pixel ratio of 1.
constexpr Core::Plot::Insets AppInsets{ .left = 0, .top = 0, .right = 64, .bottom = 22 };
constexpr std::uint32_t AppWidthPx = 960;
constexpr std::uint32_t AppHeightPx = 540;
constexpr std::uint32_t AppPlotWidthPx = AppWidthPx - AppInsets.right;

// The price axis leaves 5% of the price range free above the highest trade:
// 23 pixels here, more than the largest marker's 16. So the plot's top rows
// show the grid's vertical lines and nothing else.
constexpr std::uint32_t FreeTopRows = 6;

/** The time axis's grid lines, in the plot's top rows. */
void findGridLines(const App& app, std::vector<Scene::Patch>& patches)
{
    for (const auto& tick : app.getAxes().getTimes())
    {
        // On the pixel column the shader floors it to, in 32-bit floats as it
        // gets them; a product of two floats rounds the same on every GPU.
        const float xPx = std::floor(static_cast<float>(tick.fraction) * static_cast<float>(AppPlotWidthPx));
        if (xPx < 0.0f || xPx >= static_cast<float>(AppPlotWidthPx))
        {
            continue;
        }
        patches.push_back({ .what = std::format("grid line at {}", tick.label),
                            .xPx = static_cast<std::uint32_t>(xPx),
                            .yPx = 0,
                            .heightPx = FreeTopRows,
                            .rgba = toBytes(Gpu::Market::Type::Theme{}.plot.grid),
                            .tolerance = 1 });
    }
}

/**
 * A pixel the ring around the picked trade fully covers, in the cursor
 * color: above its center, 2.5 pixels outside its marker (the ring is 2
 * pixels wide, 1.5 outside the marker). The ring is drawn over every marker.
 */
void findRing(const App& app, std::vector<Scene::Patch>& patches)
{
    const auto* picked = std::get_if<Pointer::Picked::Trade>(&app.getPointer().getPicked());
    if (!picked)
    {
        return;
    }
    const double middlePx = app.getMarkerSizing().getRadiusCssPx(picked->amount) + 2.5;
    const double xPx = std::floor(picked->centerXPx);
    // The row whose center is nearest the middle of the ring: within half a pixel.
    const double yPx = std::floor(picked->centerYPx - middlePx);
    const double distancePx = std::hypot(xPx + 0.5 - picked->centerXPx, yPx + 0.5 - picked->centerYPx);
    if (std::abs(distancePx - middlePx) > 0.45 || yPx < 0)
    {
        return;
    }
    patches.push_back({ .what = "ring",
                        .xPx = static_cast<std::uint32_t>(xPx),
                        .yPx = static_cast<std::uint32_t>(yPx),
                        .rgba = toBytes(Gpu::Market::Type::Theme{}.cursor),
                        .tolerance = 0 });
}

/** The middle of the picked column's bar, drawn opaque in the cursor color. */
void findHighlightedColumn(const App& app, std::vector<Scene::Patch>& patches)
{
    const auto* picked = std::get_if<Pointer::Picked::Column>(&app.getPointer().getPicked());
    if (!picked)
    {
        return;
    }
    patches.push_back({ .what = "highlighted column",
                        .xPx = static_cast<std::uint32_t>(picked->centerXPx),
                        .yPx = static_cast<std::uint32_t>((picked->topPx + picked->bottomPx) / 2.0),
                        .rgba = toBytes(Gpu::Market::Type::Theme{}.cursor),
                        .tolerance = 0 });
}

/** Moves the pointer down the middle of the plot until it picks a trade or a column. */
void pointAtMiddle(App& app)
{
    const double xPx = (AppWidthPx - AppInsets.right) / 2.0;
    for (std::uint32_t yPx = 0; yPx < AppHeightPx - AppInsets.bottom; yPx += 4)
    {
        app.setPointer(xPx, yPx);
        if (!std::holds_alternative<std::monostate>(app.getPointer().getPicked()))
        {
            return;
        }
    }
}

/** Shows `trades`, which are few enough for any GPU. */
void showTrades(App& app, Core::Data::Trades trades)
{
    if (auto set = app.setTrades(std::move(trades)); !set)
    {
        Core::Log::error("{}", set.error());
    }
}

void setUpHour(App& app)
{
    showTrades(app, generateSyntheticDay());
    pointAtMiddle(app);
}

void setUpDay(App& app)
{
    showTrades(app, generateSyntheticDay());
    app.setView(Core::getUtcDay(app.getTrades().timeUs.front()));
    pointAtMiddle(app);
}

void findHourPatches(const Scene& /*scene*/, const App& app, std::vector<Scene::Patch>& patches)
{
    findGridLines(app, patches);
    findRing(app, patches);
}

void findDayPatches(const Scene& /*scene*/, const App& app, std::vector<Scene::Patch>& patches)
{
    findGridLines(app, patches);
    findHighlightedColumn(app, patches);
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
constexpr Core::Plot::Insets ColorInsets{ .left = 0, .top = 0, .right = 40, .bottom = 20 };
constexpr std::uint32_t ColorWidthPx = 400;
constexpr std::uint32_t ColorHeightPx = 200;
constexpr std::uint32_t BuyXPx = 90;
constexpr std::uint32_t SellXPx = 270;
constexpr std::uint32_t MarkersYPx = 90;
// The row whose pixel centers lie in the middle of the ring, which is fully
// covered there, around the largest marker.
constexpr float RingMiddlePx = Core::Plot::MarkerMaxRadiusCssPx + Core::Plot::RingGapCssPx + (Core::Plot::RingWidthCssPx / 2.0f);
constexpr auto RingYPx = static_cast<std::uint32_t>(static_cast<float>(MarkersYPx) - RingMiddlePx - 0.5f);

/** `patches`, then the color scenes' margins. */
std::vector<Scene::Patch> withMargins(std::vector<Scene::Patch> patches)
{
    std::ranges::move(margins(ColorWidthPx, ColorHeightPx, ColorInsets, opaque(Background), 0), std::back_inserter(patches));
    return patches;
}

void setUpColors(App& app, float opacity)
{
    Core::Data::Trades trades;
    const auto add = [&](std::int64_t timeUs, Core::Data::Side side) {
        trades.timeUs.push_back(timeUs);
        trades.price.push_back(100.0);
        trades.amount.push_back(100.0);
        trades.side.push_back(side);
    };
    add(DayStartUs + 12 * HourUs + 15 * MinuteUs, Core::Data::Side::Buy);
    add(DayStartUs + 12 * HourUs + 45 * MinuteUs, Core::Data::Side::Sell);
    showTrades(app, std::move(trades));
    app.setTheme({
        .plot = { .background = toColor(Background), .grid = toColor(Background) },
        .buy = toColor(Buy),
        .sell = toColor(Sell),
        .markerOpacity = opacity,
        .cursor = toColor(Cursor),
    });
}

void setUpOpaque(App& app)
{
    setUpColors(app, 1.0f);
    app.setPointer(BuyXPx, MarkersYPx);
}

void setUpBlended(App& app)
{
    setUpColors(app, BlendedOpacity);
}

const std::vector<Scene>& getAll()
{
    static const std::vector<Scene> Scenes{
        {
            .name = "hour",
            .description = "Synthetic trades from 12:00 to 13:00 as markers, one in the middle ringed",
            .widthPx = AppWidthPx,
            .heightPx = AppHeightPx,
            .insets = AppInsets,
            .patches = margins(AppWidthPx, AppHeightPx, AppInsets, toBytes(Gpu::Market::Type::Theme{}.plot.background), 1),
            .setUp = setUpHour,
            .findPatches = findHourPatches,
        },
        {
            .name = "day",
            .description = "The whole synthetic day as column summaries, the one in the middle highlighted",
            .widthPx = AppWidthPx,
            .heightPx = AppHeightPx,
            .insets = AppInsets,
            .patches = margins(AppWidthPx, AppHeightPx, AppInsets, toBytes(Gpu::Market::Type::Theme{}.plot.background), 1),
            .setUp = setUpDay,
            .findPatches = findDayPatches,
        },
        {
            .name = "opaque",
            .description = "A buy and a sell in opaque colors chosen for the test, the buy ringed",
            .widthPx = ColorWidthPx,
            .heightPx = ColorHeightPx,
            .insets = ColorInsets,
            .patches = withMargins({
                { .what = "buy marker", .xPx = BuyXPx, .yPx = MarkersYPx, .rgba = opaque(Buy), .tolerance = 0 },
                { .what = "sell marker", .xPx = SellXPx, .yPx = MarkersYPx, .rgba = opaque(Sell), .tolerance = 0 },
                { .what = "ring", .xPx = BuyXPx, .yPx = RingYPx, .rgba = opaque(Cursor), .tolerance = 0 },
                { .what = "between the markers",
                  .xPx = (BuyXPx + SellXPx) / 2,
                  .yPx = MarkersYPx,
                  .rgba = opaque(Background),
                  .tolerance = 0 },
            }),
            .setUp = setUpOpaque,
        },
        {
            .name = "blended",
            .description = "The same buy and sell at half opacity over the background",
            .widthPx = ColorWidthPx,
            .heightPx = ColorHeightPx,
            .insets = ColorInsets,
            .patches = withMargins({
                { .what = "buy marker", .xPx = BuyXPx, .yPx = MarkersYPx, .rgba = over(Buy, Background, BlendedOpacity), .tolerance = 1 },
                { .what = "sell marker",
                  .xPx = SellXPx,
                  .yPx = MarkersYPx,
                  .rgba = over(Sell, Background, BlendedOpacity),
                  .tolerance = 1 },
            }),
            .setUp = setUpBlended,
        },
    };
    return Scenes;
}

}  // namespace

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

std::vector<Scene::Patch> Scene::show(const Scene& scene, App& app)
{
    app.setSize(scene.widthPx, scene.heightPx);
    app.setPixelRatio(1.0f);
    app.setPlotInsets(scene.insets);
    app.setSummariesAllowed(true);
    app.setMarkerScale(Core::Plot::MarkerScale::Area);
    app.setTheme({});
    app.clearPointer();
    scene.setUp(app);

    std::vector<Patch> patches = scene.patches;
    if (scene.findPatches)
    {
        scene.findPatches(scene, app, patches);
    }
    return patches;
}

std::vector<std::string> Scene::checkPatches(const Scene& scene, std::span<const Patch> patches, std::span<const std::uint8_t> rgba)
{
    if (rgba.size() != std::size_t{ scene.widthPx } * scene.heightPx * 4)
    {
        return { std::format("{} bytes, not {} × {} pixels", rgba.size(), scene.widthPx, scene.heightPx) };
    }
    std::vector<std::string> failures;
    for (const auto& patch : patches)
    {
        std::size_t off = 0;
        std::string first;
        for (std::uint32_t yPx = patch.yPx; yPx < patch.yPx + patch.heightPx; ++yPx)
        {
            for (std::uint32_t xPx = patch.xPx; xPx < patch.xPx + patch.widthPx; ++xPx)
            {
                const std::size_t i = (std::size_t{ yPx } * scene.widthPx + xPx) * 4;
                const bool close =
                    xPx < scene.widthPx && yPx < scene.heightPx
                    && std::ranges::all_of(
                        std::array{ 0, 1, 2, 3 },
                        [&](std::size_t channel) { return std::abs(rgba[i + channel] - patch.rgba[channel]) <= patch.tolerance; });
                if (!close && off++ == 0)
                {
                    first = xPx < scene.widthPx && yPx < scene.heightPx
                                ? std::format("({}, {}) is ({}, {}, {}, {})", xPx, yPx, rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3])
                                : std::format("({}, {}) is outside the scene", xPx, yPx);
                }
            }
        }
        if (off > 0)
        {
            failures.push_back(std::format("{}: {} of {} pixels aren't ({}, {}, {}, {}) ±{}; {}", patch.what, off,
                                           std::size_t{ patch.widthPx } * patch.heightPx, patch.rgba[0], patch.rgba[1], patch.rgba[2],
                                           patch.rgba[3], patch.tolerance, first));
        }
    }
    return failures;
}

}  // namespace Viewer
