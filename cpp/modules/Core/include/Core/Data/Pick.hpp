#pragma once

#include "Core/Data/Trades.hpp"
#include "Core/Range.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Core::Data {

/** Where trades are drawn: the view's ranges stretched over a plot `width` × `height` pixels, price up. */
struct PlotMapping
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
    /** The nearest trade's centre must be this close. */
    double pickRadius;
    /** No marker is bigger, so a trade further away can't be under the pointer. */
    double maxMarkerRadius;
};

/**
 * The trade the pointer at (`x`, `y`), in pixels from the plot's top left
 * corner, points at, among the `visible` ones:
 *
 * 1. The trade whose centre is nearest, by straight-line distance on screen,
 *    if it is within `rule.pickRadius`.
 * 2. Otherwise, a trade whose marker the pointer is inside: within
 *    `markerRadius(amount)` of its centre.
 *
 * Ties go to the later trade, which is drawn on top. Nothing outside the plot.
 */
template<typename MarkerRadius>
[[nodiscard]] std::optional<std::size_t> pickTrade(const Trades& trades, IndexRange visible, const PlotMapping& plot,
                                                   double x, double y, const PickRule& rule, MarkerRadius markerRadius)
{
    if (!(plot.width > 0.0 && plot.height > 0.0 && plot.time.getLengthUs() > 0 && plot.price.high > plot.price.low)
        || x < 0.0 || y < 0.0 || x > plot.width || y > plot.height) {
        return std::nullopt;
    }

    // Only trades within `reach` pixels in time can be picked by either rule.
    const double reach = std::max(rule.pickRadius, rule.maxMarkerRadius);
    const double usPerPixel = static_cast<double>(plot.time.getLengthUs()) / plot.width;
    const TimeRange window{
        .startUs = plot.time.startUs + static_cast<std::int64_t>(std::floor((x - reach) * usPerPixel)),
        .endUs = plot.time.startUs + static_cast<std::int64_t>(std::ceil((x + reach) * usPerPixel)) + 1,
    };
    const IndexRange near = getTradesIn(trades, window);
    const std::size_t first = std::max(near.first, visible.first);
    const std::size_t last = std::min(near.last, visible.last);

    std::optional<std::size_t> nearest;
    double nearestSquared = rule.pickRadius * rule.pickRadius;
    std::optional<std::size_t> under;
    for (std::size_t i = first; i < last; ++i) {
        const double dx = plot.getX(trades.timeUs[i]) - x;
        const double dy = plot.getY(trades.price[i]) - y;
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

} // namespace Core::Data
