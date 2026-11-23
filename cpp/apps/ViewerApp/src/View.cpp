#include "Viewer/View.hpp"

#include <algorithm>
#include <cstdint>

namespace Viewer {

namespace {

// How far the time range can zoom.
constexpr std::int64_t MinViewUs = 1'000;
constexpr std::int64_t MaxViewUs = 7 * 86'400'000'000;

// Space above and below the visible trades, as a share of their price range.
constexpr double PricePadding = 0.05;

}  // namespace

Core::Plot::Mapping View::getMapping(const Layout& layout) const
{
    return { .time = mTime, .price = mPrice, .width = layout.getPlotWidthPx(), .height = layout.getPlotHeightPx() };
}

void View::setTimeRange(const Core::Data::PreprocessedTrades& data, Core::TimeRange time)
{
    mTime = time;
    mTrades = Core::Data::getTradesIn(data.getTrades(), time);
    if (const auto price = data.getPriceRange(mTrades))
    {
        // Some room even for a single price, so its trades sit mid-height.
        const double padding = std::max((price->high - price->low) * PricePadding, price->high * 1e-5);
        mPrice = { .low = price->low - padding, .high = price->high + padding };
    }
    if (mTrades.empty())
    {
        mStats = {};
        return;
    }
    const auto& timeUs = data.getTrades().timeUs;
    mStats = {
        .count = mTrades.size(),
        .firstUs = timeUs[mTrades.first],
        .lastUs = timeUs[mTrades.last - 1],
        .totalAmount = data.getAmount(mTrades),
    };
}

void View::zoomAt(const Core::Data::PreprocessedTrades& data, const Layout& layout, double xPx, double factor)
{
    const auto length = static_cast<double>(mTime.getLengthUs());
    const double newLength = std::clamp(length * factor, static_cast<double>(MinViewUs), static_cast<double>(MaxViewUs));
    const double plotWidthPx = layout.getPlotWidthPx();
    const double anchor = plotWidthPx > 0.0 ? (xPx - layout.insets.left) / plotWidthPx : 0.5;
    setTimeRange(data, mTime.zoomed(anchor, newLength / length));
}

void View::panBy(const Core::Data::PreprocessedTrades& data, const Layout& layout, double dxPx)
{
    if (const double plotWidthPx = layout.getPlotWidthPx(); plotWidthPx > 0.0)
    {
        setTimeRange(data, mTime.panned(-dxPx / plotWidthPx));
    }
}

}  // namespace Viewer
