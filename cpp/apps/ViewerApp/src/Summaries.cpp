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
    const double columnCount = std::round(layout.getPlotWidthPx() / layout.pixelRatio);
    const bool crowded = mAllowed && columnCount >= 1.0 && static_cast<double>(view.getTrades().size()) > mThreshold * columnCount;
    if (!crowded)
    {
        if (mColumnCount == 0)
        {
            return false;
        }
        mColumnCount = 0;
        mColumns.clear();
        return true;
    }
    mColumnCount = static_cast<std::uint32_t>(columnCount);
    mColumns = Core::Data::summarizeColumns(data, view.getTimeRange(), mColumnCount);
    return true;
}

}  // namespace Viewer
