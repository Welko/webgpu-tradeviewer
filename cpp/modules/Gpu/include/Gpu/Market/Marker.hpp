#pragma once

#include "Gpu/Market/Type.hpp"

namespace Gpu::Market::Marker {

// How big trades are drawn. The trades pass passes these to its shader, so a
// legend built from them matches what's on screen. Colours are in Type::Theme.

// Radii in CSS or window pixels. Every scale stays between these.
inline constexpr float MinRadius = 1.5f;
inline constexpr float MaxRadius = 16.0f;
inline constexpr float AreaRadiusAt1Btc = 6.0f;
// The log scale grows from MinRadius at LogLowest BTC to MaxRadius at LogHighest BTC.
inline constexpr float LogLowest = 1e-4f;
inline constexpr float LogHighest = 100.0f;

/** The radius of a marker for `amount` BTC, in CSS or window pixels, as the shader computes it. */
[[nodiscard]] float getRadius(double amount, Type::MarkerScale scale);

/**
 * Colours from `theme`, and reference sizes for a legend: where the scale
 * stops growing, and a few round amounts in between.
 */
[[nodiscard]] Type::Legend getLegend(Type::MarkerScale scale, const Type::Theme& theme);

} // namespace Gpu::Market::Marker
