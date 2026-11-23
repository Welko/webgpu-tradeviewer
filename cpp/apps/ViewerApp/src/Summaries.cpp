#include "Viewer/Summaries.hpp"

#include <cmath>

namespace Viewer {

std::size_t Summaries::getBytes() const
{
    return mColumns.capacity() * sizeof(Core::Data::ColumnSummary);
}

bool Summaries::update(const Core::Data::PreprocessedTrades& data, const View& view, const Layout& layout)
{
    // One column per CSS or window pixel.
    const double across = std::round(layout.getPlotWidthPx() / layout.pixelRatio);
    const bool crowded = mAllowed && across >= 1.0 && static_cast<double>(view.getTrades().size()) > mThreshold * across;
    if (!crowded)
    {
        if (mAcross == 0)
        {
            return false;
        }
        mAcross = 0;
        mColumns.clear();
        return true;
    }
    mAcross = static_cast<std::uint32_t>(across);
    mColumns = Core::Data::summarizeColumns(data, view.getTimeRange(), mAcross);
    return true;
}

}  // namespace Viewer
