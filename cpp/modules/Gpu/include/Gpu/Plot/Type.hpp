#pragma once

#include "Gpu/Type.hpp"

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

/** The plot's colours. */
struct Theme
{
    Gpu::Type::Color background{ .red = 0.07f, .green = 0.08f, .blue = 0.10f };
    /** Of the grid lines, which are opaque. */
    Gpu::Type::Color grid{ .red = 0.145f, .green = 0.155f, .blue = 0.17f };
};

} // namespace Gpu::Plot::Type
