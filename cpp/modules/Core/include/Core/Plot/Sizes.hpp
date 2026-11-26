#pragma once

#include "Core/Data/Trades.hpp"

#include <cstdint>

/**
 * The sizes on the plot that drawing and picking share: the space around it,
 * and how big markers and column bars are, so what's picked is exactly what's
 * drawn.
 */
namespace Core::Plot {

/** Space left around the plot, for axis labels, in pixels. */
struct Insets
{
    std::uint32_t left;
    std::uint32_t top;
    std::uint32_t right;
    std::uint32_t bottom;

    bool operator==(const Insets&) const = default;
};

// Marker radii. Every scale stays between these.
inline constexpr float MarkerMinRadiusCssPx = 1.5f;
inline constexpr float MarkerMaxRadiusCssPx = 16.0f;

// How far up the trades sorted by amount, from 0 to 1, marker sizes are fitted
// to: past these, a few extreme trades would shrink all the others.
inline constexpr double MarkerFitLowShare = 0.01;
inline constexpr double MarkerFitHighShare = 0.999;

// The ring around the highlighted trade: a gap outside its marker, then the
// ring this wide.
inline constexpr float RingGapCssPx = 1.5f;
inline constexpr float RingWidthCssPx = 2.0f;

// Column summaries' bars, drawn instead of markers in crowded views, are at
// least this tall, so a column of one price still shows.
inline constexpr float ColumnMinHeightCssPx = 2.0f;

/** How a trade's marker size shows its amount. */
enum class MarkerScale : std::uint8_t
{
    Area,  ///< Area proportional to the amount.
    Log,  ///< Radius growing with the amount's logarithm, which separates small trades better.
};

/**
 * How a trade's amount becomes its marker's size. The area scale reaches the
 * largest marker at `highAmount`; the log scale grows from the smallest at
 * `lowAmount` to the largest at `highAmount`. Both amounts are above zero, and
 * `highAmount` at least ten times `lowAmount`.
 */
struct MarkerSizing
{
    MarkerScale scale = MarkerScale::Area;
    double lowAmount = 0.01;
    double highAmount = 1.0;

    /** The radius of a marker for `amount`, as the shader computes it. */
    [[nodiscard]] float getRadiusCssPx(double amount) const;

    bool operator==(const MarkerSizing&) const = default;
};

/**
 * `scale` fitted to `trades`, between MarkerFitLowShare and MarkerFitHighShare
 * of the way up them by amount, so any market's trades get the full range of
 * sizes. Trades of a single amount still get a scale, and no trades the
 * default one. Sorts a copy of the amounts: for when trades change.
 */
[[nodiscard]] MarkerSizing fitMarkerSizing(MarkerScale scale, const Data::Trades& trades);

}  // namespace Core::Plot
