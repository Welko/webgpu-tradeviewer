#include "Viewer/Layout.hpp"

#include <algorithm>

namespace Viewer {

double Layout::getPlotWidthPx() const
{
    return std::max(0.0, static_cast<double>(widthPx) - insets.left - insets.right);
}

double Layout::getPlotHeightPx() const
{
    return std::max(0.0, static_cast<double>(heightPx) - insets.top - insets.bottom);
}

} // namespace Viewer
