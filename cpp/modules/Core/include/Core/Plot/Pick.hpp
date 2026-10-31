#pragma once

#include "Core/Data/Columns.hpp"
#include "Core/Data/Trades.hpp"
#include "Core/Range.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Core::Plot {

/** Where trades are drawn: the view's ranges stretched over a plot `width` × `height` pixels, price up. */
struct Mapping
{
    TimeRange time;
    ValueRange price;
    double width;
    double height;

    /** Pixels from the plot's left edge. */
    [[nodiscard]] double getX(std::int64_t timeUs) const
    {
        return static_cast<double>(timeUs - time.startUs) / static_cast<double>(time.getLengthUs()) * width;
    }

    /** Pixels from the plot's top edge. */
    [[nodiscard]] double getY(double value) const { return (price.high - value) / (price.high - price.low) * height; }
};

/** How far picking looks, in pixels. */
struct PickRule
{
    /** The nearest trade's center must be this close. */
    double pickRadius;
    /** No marker is bigger, so a trade further away can't be under the pointer. */
    double maxMarkerRadius;
};

/**
 * The trade the pointer at (`pointerX`, `pointerY`), in pixels from the
 * plot's top left corner, points at, among the `visible` ones:
 *
 * 1. The trade whose center is nearest, by straight-line distance on screen,
 *    if it is within `rule.pickRadius`.
 * 2. Otherwise, a trade whose marker the pointer is inside: within
 *    `markerRadius(amount)` of its center.
 *
 * Ties go to the later trade, which is drawn on top. Nothing outside the plot.
 */
template<typename MarkerRadius>
[[nodiscard]] std::optional<std::size_t> pickTrade(const Data::Trades& trades, IndexRange visible, const Mapping& plot,
                                                   double pointerX, double pointerY, const PickRule& rule,
                                                   MarkerRadius markerRadius)
{
    if (!(plot.width > 0.0 && plot.height > 0.0 && plot.time.getLengthUs() > 0 && plot.price.high > plot.price.low)
        || pointerX < 0.0 || pointerY < 0.0 || pointerX > plot.width || pointerY > plot.height) {
        return std::nullopt;
    }

    // Only trades within `reach` pixels in time can be picked by either rule.
    const double reach = std::max(rule.pickRadius, rule.maxMarkerRadius);
    const double usPerPixel = static_cast<double>(plot.time.getLengthUs()) / plot.width;
    const TimeRange window{
        .startUs = plot.time.startUs + static_cast<std::int64_t>(std::floor((pointerX - reach) * usPerPixel)),
        .endUs = plot.time.startUs + static_cast<std::int64_t>(std::ceil((pointerX + reach) * usPerPixel)) + 1,
    };
    const IndexRange near = Data::getTradesIn(trades, window);
    const std::size_t first = std::max(near.first, visible.first);
    const std::size_t last = std::min(near.last, visible.last);

    std::optional<std::size_t> nearest;
    double nearestSquared = rule.pickRadius * rule.pickRadius;
    std::optional<std::size_t> under;
    for (std::size_t i = first; i < last; ++i) {
        const double dx = plot.getX(trades.timeUs[i]) - pointerX;
        const double dy = plot.getY(trades.price[i]) - pointerY;
        const double squared = dx * dx + dy * dy;
        if (squared <= nearestSquared) {
            nearest = i;
            nearestSquared = squared;
        }
        if (!nearest) {
            const double radius = markerRadius(trades.amount[i]);
            if (squared <= radius * radius) {
                under = i;
            }
        }
    }
    return nearest ? nearest : under;
}

/** Where a column summary's bar is drawn, in pixels from the plot's top left corner. */
struct ColumnBar
{
    double centerX;
    double top;
    double bottom;
};

/**
 * Column `column.index` of the `across` that split the plot, as a vertical
 * line through its center from its highest to its lowest price, at least
 * `minHeight` pixels long.
 */
[[nodiscard]] inline ColumnBar getColumnBar(const Data::ColumnSummary& column, std::size_t across, const Mapping& plot,
                                            double minHeight)
{
    double top = plot.getY(column.price.high);
    double bottom = plot.getY(column.price.low);
    const double grow = std::max(0.0, minHeight - (bottom - top)) / 2.0;
    top -= grow;
    bottom += grow;
    const double centerX = (static_cast<double>(column.index) + 0.5) * plot.width / static_cast<double>(across);
    return { .centerX = centerX, .top = top, .bottom = bottom };
}

/**
 * The column summary the pointer at (`pointerX`, `pointerY`) points at, by its
 * position in `columns`: the one whose bar (see getColumnBar) is nearest, by
 * straight-line distance on screen, within `pickRadius`. Ties go to the later
 * column. `columns` holds only those with trades, of the `across` that split
 * the plot.
 */
[[nodiscard]] inline std::optional<std::size_t> pickColumn(std::span<const Data::ColumnSummary> columns, std::size_t across,
                                                           const Mapping& plot, double pointerX, double pointerY,
                                                           double pickRadius, double minHeight)
{
    if (across == 0 || !(plot.width > 0.0 && plot.height > 0.0 && plot.price.high > plot.price.low) || pointerX < 0.0
        || pointerY < 0.0 || pointerX > plot.width || pointerY > plot.height) {
        return std::nullopt;
    }
    std::optional<std::size_t> nearest;
    double nearestSquared = pickRadius * pickRadius;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const ColumnBar bar = getColumnBar(columns[i], across, plot, minHeight);
        const double dx = bar.centerX - pointerX;
        const double dy = pointerY < bar.top ? bar.top - pointerY : (pointerY > bar.bottom ? pointerY - bar.bottom : 0.0);
        const double squared = dx * dx + dy * dy;
        if (squared <= nearestSquared) {
            nearest = i;
            nearestSquared = squared;
        }
    }
    return nearest;
}

} // namespace Core::Plot
