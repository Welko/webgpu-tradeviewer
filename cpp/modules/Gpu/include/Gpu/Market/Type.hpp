#pragma once

#include "Gpu/Plot/Type.hpp"
#include "Gpu/Type.hpp"

#include "Core/Range.hpp"

#include <cstdint>
#include <vector>

namespace Gpu::Market::Type {

/** How a trade's marker size shows its amount. */
enum class MarkerScale : std::uint8_t
{
    Area,  ///< Area proportional to the amount.
    Log,   ///< Radius growing with the amount's logarithm, which separates small trades better.
};

/** Colors of the plot and the trades. */
struct Theme
{
    Plot::Type::Theme plot;
    Gpu::Type::Color buy{ .red = 0.20f, .green = 0.80f, .blue = 0.45f };
    Gpu::Type::Color sell{ .red = 0.95f, .green = 0.30f, .blue = 0.30f };
    /** Of every marker, over the background. */
    float markerOpacity = 0.6f;
    /** Of the ring around the trade under the cursor. */
    Gpu::Type::Color cursor{ .red = 1.0f, .green = 1.0f, .blue = 1.0f };
};

/** One marker size a legend shows. */
struct LegendSize
{
    /** Where the scale stops growing, a size stands for every amount past it too. */
    enum class Bound : std::uint8_t
    {
        Exact,    ///< Just this amount.
        AtMost,   ///< This amount and all smaller ones.
        AtLeast,  ///< This amount and all larger ones.
    };

    double amount = 0.0;  // BTC
    float radiusCssPx = 0.0f;
    Bound bound = Bound::Exact;
};

/** What the trade markers look like, for a legend to match them. */
struct Legend
{
    Gpu::Type::Color buy;
    Gpu::Type::Color sell;
    float opacity = 1.0f;  // of every marker, over the background
    std::vector<LegendSize> sizes;  // smallest first
};

/** The trades in one column of the plot, summed up, to draw as a bar instead of their markers. */
struct Column
{
    std::uint32_t index;  // of Columns::across, from the plot's left
    Core::ValueRange price;  // lowest and highest traded
    double buyAmount;
    double sellAmount;
};

/**
 * Summaries to draw instead of the view's trades. The plot is split into
 * `across` columns of equal width, which places each one; only those with
 * trades are listed, so `columns.size()` can be smaller than `across`, with
 * gaps where nothing traded.
 */
struct Columns
{
    std::uint32_t across = 0;
    std::vector<Column> columns;  // in any order
};

/** The part of the trades on screen. */
struct View
{
    /** Left to right edge. */
    Core::TimeRange time;
    /** Bottom to top edge. */
    Core::ValueRange price;
    /** The trades in `time`: the ones drawn. */
    Core::IndexRange trades;
};

} // namespace Gpu::Market::Type
