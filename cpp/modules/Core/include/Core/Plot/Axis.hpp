#pragma once

#include "Core/Range.hpp"

#include <cstddef>
#include <string>
#include <vector>

/**
 * Where an axis gets its grid lines and labels: round values, spaced so that
 * at most a given number fit.
 */
namespace Core::Plot::Axis {

struct Tick
{
    /** Along the axis: 0 at the start (left, bottom), 1 at the end. */
    double fraction;
    std::string label;
};

/**
 * Ticks at whole multiples of a round step, from 1 µs through 1 ms, 1 s,
 * 1 min and 1 h up to days, in UTC. Labels show as much precision as the
 * step needs ("12:30", "12:30:05.250"), and the date at midnight.
 */
[[nodiscard]] std::vector<Tick> getTimeTicks(TimeRange range, std::size_t maxCount);

/** Ticks at multiples of 1, 2 or 5 times a power of ten, labeled with the decimals the step needs. */
[[nodiscard]] std::vector<Tick> getNumberTicks(ValueRange range, std::size_t maxCount);

} // namespace Core::Plot::Axis
