#pragma once

#include "Gpu/Market/Type.hpp"

namespace Gpu::Market::Style {

// The shapes trades are drawn with: marker sizes, and column summaries' bars.
// The passes give these to their shaders, so a legend built from them matches
// what's on screen. Colors are in Type::Theme.

// Marker radii. Every scale stays between these.
inline constexpr float MarkerMinRadiusCssPx = 1.5f;
inline constexpr float MarkerMaxRadiusCssPx = 16.0f;
inline constexpr float MarkerAreaRadiusAt1BtcCssPx = 6.0f;
// The log scale grows from MarkerMinRadiusCssPx at MarkerLogLowest BTC to
// MarkerMaxRadiusCssPx at MarkerLogHighest BTC.
inline constexpr float MarkerLogLowest = 1e-4f;
inline constexpr float MarkerLogHighest = 100.0f;

// Column summaries, drawn instead of markers in crowded views: bars at least
// this tall, so a column of one price still shows, and this opaque for the
// column with the least traded (the most is opaque).
inline constexpr float ColumnMinHeightCssPx = 2.0f;
inline constexpr float ColumnMinOpacity = 0.35f;

/** The radius of a marker for `amount` BTC, as the shader computes it. */
[[nodiscard]] float getMarkerRadiusCssPx(double amount, Type::MarkerScale scale);

/**
 * Colors from `theme`, and reference sizes for a legend: where the scale
 * stops growing, and a few round amounts in between.
 */
[[nodiscard]] Type::Legend getLegend(Type::MarkerScale scale, const Type::Theme& theme);

} // namespace Gpu::Market::Style
