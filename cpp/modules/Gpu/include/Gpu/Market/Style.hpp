#pragma once

#include "Gpu/Market/Type.hpp"

#include "Core/Plot/Sizes.hpp"

namespace Gpu::Market::Style {

// How trades are drawn beyond their sizes (see Core/Plot/Sizes.hpp). Colors
// are in Type::Theme.

// The column summary with the least traded is drawn this opaque; the most is opaque.
inline constexpr float ColumnMinOpacity = 0.35f;

/**
 * Colors from `theme`, and reference sizes for a legend of `sizing`: where the
 * scale stops growing, and a few round amounts between.
 */
[[nodiscard]] Type::Legend getLegend(const Core::Plot::MarkerSizing& sizing, const Type::Theme& theme);

}  // namespace Gpu::Market::Style
