#include "Viewer/Pointer.hpp"

#include "Core/Plot/Pick.hpp"

namespace Viewer {

namespace {

// How far from the pointer a trade's center can be picked, in CSS or window pixels.
constexpr double PickRadiusCssPx = 24.0;

}  // namespace

std::optional<std::size_t> Pointer::getHighlightedTrade() const
{
    const auto* trade = std::get_if<Picked::Trade>(&mPicked);
    return trade ? std::optional(trade->index) : std::nullopt;
}

void Pointer::setPosition(double xPx, double yPx)
{
    mOnSurface = true;
    mXPx = xPx;
    mYPx = yPx;
}

void Pointer::clear()
{
    mOnSurface = false;
}

void Pointer::update(const Core::Data::PreprocessedTrades& data, const View& view, const Summaries& summaries, const Layout& layout,
                     const Gpu::Market::Style::MarkerSizing& markerSizing)
{
    mPicked = std::monostate{};
    mColumnPosition.reset();
    if (!mOnSurface)
    {
        return;
    }

    const double ratio = layout.pixelRatio;
    const auto plot = view.getMapping(layout);
    const double pointerXPx = mXPx - layout.insets.left;
    const double pointerYPx = mYPx - layout.insets.top;
    const auto across = summaries.getNumColumns();
    if (across > 0)
    {
        const auto& columns = summaries.getColumns();
        const double minHeightPx = Gpu::Market::Style::ColumnMinHeightCssPx * ratio;
        mColumnPosition = Core::Plot::pickColumn(columns, across, plot, pointerXPx, pointerYPx, PickRadiusCssPx * ratio, minHeightPx);
        if (!mColumnPosition)
        {
            return;
        }
        const auto& column = columns[*mColumnPosition];
        const auto bar = Core::Plot::getColumnBar(column, across, plot, minHeightPx);
        const auto time = view.getTimeRange();
        const std::int64_t length = time.getLengthUs();
        const auto count = static_cast<std::int64_t>(across);
        const auto index = static_cast<std::int64_t>(column.index);
        mPicked = Picked::Column{
            .summary = column,
            .time = { .startUs = time.startUs + index * length / count, .endUs = time.startUs + (index + 1) * length / count },
            .centerXPx = layout.insets.left + bar.centerX,
            .topPx = layout.insets.top + bar.top,
            .bottomPx = layout.insets.top + bar.bottom,
        };
        return;
    }

    const Core::Plot::PickRule rule{ .pickRadius = PickRadiusCssPx * ratio,
                                     .maxMarkerRadius = Gpu::Market::Style::MarkerMaxRadiusCssPx * ratio };
    const auto& trades = data.getTrades();
    const auto i = Core::Plot::pickTrade(trades, view.getTrades(), plot, pointerXPx, pointerYPx, rule,
                                         [&](double amount) { return markerSizing.getRadiusCssPx(amount) * ratio; });
    if (!i)
    {
        return;
    }
    mPicked = Picked::Trade{
        .index = *i,
        .timeUs = trades.timeUs[*i],
        .price = trades.price[*i],
        .amount = trades.amount[*i],
        .side = trades.side[*i],
        .centerXPx = layout.insets.left + plot.getX(trades.timeUs[*i]),
        .centerYPx = layout.insets.top + plot.getY(trades.price[*i]),
    };
}

}  // namespace Viewer
