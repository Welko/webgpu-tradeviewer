#pragma once

#include "Gpu/Plot/Type.hpp"

#include <cstdint>

namespace Viewer {

/** The surface, and where on it the plot goes. */
struct Layout
{
    std::uint32_t widthPx = 0;
    std::uint32_t heightPx = 0;
    float pixelRatio = 1.0f;
    Gpu::Plot::Type::Insets insets{};

    [[nodiscard]] double getPlotWidthPx() const;
    [[nodiscard]] double getPlotHeightPx() const;
};

} // namespace Viewer
