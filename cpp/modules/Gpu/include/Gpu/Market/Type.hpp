#pragma once

#include "Core/Data/TimeRange.hpp"
#include "Core/Data/Trades.hpp"

#include <cstdint>

namespace Gpu::Market::Type {

/** How a trade's marker size shows its amount. */
enum class MarkerScale : std::uint8_t
{
    Area,  ///< Area proportional to the amount.
    Log,   ///< Radius growing with the amount's logarithm, which separates small trades better.
};

/** The part of the trades on screen. */
struct View
{
    /** Left to right edge. */
    Core::Data::TimeRange time;
    /** Bottom to top edge. */
    Core::Data::PriceRange price;
    /** The trades in `time`: the ones drawn. */
    Core::Data::IndexRange trades;
};

} // namespace Gpu::Market::Type
