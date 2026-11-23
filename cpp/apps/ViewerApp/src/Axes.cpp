#include "Viewer/Axes.hpp"

#include <algorithm>
#include <cstddef>

namespace Viewer {

namespace {

// The closest ticks get, in CSS or window pixels, so their labels fit.
constexpr double TimeTickSpacingCssPx = 120.0;
constexpr double PriceTickSpacingCssPx = 50.0;

}  // namespace

Gpu::Plot::Type::Grid Axes::getGrid() const
{
    Gpu::Plot::Type::Grid grid;
    for (const auto& tick : mTimes)
    {
        grid.verticalLines.push_back(static_cast<float>(tick.fraction));
    }
    for (const auto& tick : mPrices)
    {
        grid.horizontalLines.push_back(static_cast<float>(tick.fraction));
    }
    return grid;
}

void Axes::update(const View& view, const Layout& layout)
{
    // At least two ticks, so even a small plot shows a scale.
    const auto countFitting = [&](double lengthPx, double spacingCssPx) {
        return std::max<std::size_t>(2, static_cast<std::size_t>(lengthPx / (spacingCssPx * layout.pixelRatio)));
    };
    mTimes = Core::Plot::Axis::getTimeTicks(view.getTimeRange(), countFitting(layout.getPlotWidthPx(), TimeTickSpacingCssPx));
    mPrices = Core::Plot::Axis::getNumberTicks(view.getPriceRange(), countFitting(layout.getPlotHeightPx(), PriceTickSpacingCssPx));
}

}  // namespace Viewer
