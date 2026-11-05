#pragma once

#include "Gpu/Market/Type.hpp"

#include "Core/Data/Trades.hpp"

namespace Gpu::Market::Style {

// The shapes trades are drawn with: marker sizes, and column summaries' bars.
// The passes give these to their shaders, so a legend built from them matches
// what's on screen. Colors are in Type::Theme.

// Marker radii. Every scale stays between these.
inline constexpr float MarkerMinRadiusCssPx = 1.5f;
inline constexpr float MarkerMaxRadiusCssPx = 16.0f;

// How far up the trades sorted by amount, from 0 to 1, marker sizes are fitted
// to: past these, a few extreme trades would shrink all the others.
inline constexpr double MarkerFitLowShare = 0.01;
inline constexpr double MarkerFitHighShare = 0.999;

// Column summaries, drawn instead of markers in crowded views: bars at least
// this tall, so a column of one price still shows, and this opaque for the
// column with the least traded (the most is opaque).
inline constexpr float ColumnMinHeightCssPx = 2.0f;
inline constexpr float ColumnMinOpacity = 0.35f;

/**
 * How a trade's amount becomes its marker's size. The area scale reaches the
 * largest marker at `highAmount`; the log scale grows from the smallest at
 * `lowAmount` to the largest at `highAmount`. Both amounts are above zero, and
 * `highAmount` at least ten times `lowAmount`.
 */
struct MarkerSizing
{
    Type::MarkerScale scale = Type::MarkerScale::Area;
    double lowAmount = 0.01;
    double highAmount = 1.0;

    /** The radius of a marker for `amount`, as the shader computes it. */
    [[nodiscard]] float getRadiusCssPx(double amount) const;

    bool operator==(const MarkerSizing&) const = default;
};

/**
 * `scale` fitted to `trades`, between MarkerFitLowShare and MarkerFitHighShare
 * of the way up them by amount, so any market's trades get the full range of
 * sizes. Trades of a single amount still get a scale, and no trades the
 * default one. Sorts a copy of the amounts: for when trades change.
 */
[[nodiscard]] MarkerSizing fitMarkerSizing(Type::MarkerScale scale, const Core::Data::Trades& trades);

/**
 * Colors from `theme`, and reference sizes for a legend of `sizing`: where the
 * scale stops growing, and a few round amounts between.
 */
[[nodiscard]] Type::Legend getLegend(const MarkerSizing& sizing, const Type::Theme& theme);

} // namespace Gpu::Market::Style
