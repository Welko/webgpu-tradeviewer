#pragma once

#include <cstdint>
#include <vector>

namespace Gpu::Plot::Type {

/** Space left around the plot, for axis labels, in physical pixels. */
struct Insets
{
    std::uint32_t left;
    std::uint32_t top;
    std::uint32_t right;
    std::uint32_t bottom;
};

/** Where grid lines go, as fractions of the plot: from its left edge, or from its bottom edge. */
struct Grid
{
    std::vector<float> verticalLines;
    std::vector<float> horizontalLines;
};

} // namespace Gpu::Plot::Type
