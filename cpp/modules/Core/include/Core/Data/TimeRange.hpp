#pragma once

#include <cmath>
#include <cstdint>

namespace Core::Data {

/** A time interval [startUs, endUs): start inclusive, end exclusive. */
struct TimeRange
{
    std::int64_t startUs;
    std::int64_t endUs;

    [[nodiscard]] std::int64_t getLengthUs() const { return endUs - startUs; }

    /**
     * Scaled by `factor` (above 1 zooms out) around the point `anchor` of the
     * way along it, from 0 at the start to 1 at the end. That point stays put.
     */
    [[nodiscard]] TimeRange zoomed(double anchor, double factor) const
    {
        // Doubles hold whole microseconds exactly up to 2^53, about 285 years.
        const double length = static_cast<double>(getLengthUs());
        const double anchorUs = static_cast<double>(startUs) + anchor * length;
        const std::int64_t newLength = std::llround(length * factor);
        const std::int64_t start = std::llround(anchorUs - anchor * static_cast<double>(newLength));
        return { .startUs = start, .endUs = start + newLength };
    }

    /** Moved later by `fraction` of its length, or earlier if negative. */
    [[nodiscard]] TimeRange panned(double fraction) const
    {
        const std::int64_t shift = std::llround(fraction * static_cast<double>(getLengthUs()));
        return { .startUs = startUs + shift, .endUs = endUs + shift };
    }

    bool operator==(const TimeRange&) const = default;
};

} // namespace Core::Data
