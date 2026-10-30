#include "Core/Range.hpp"

#include <doctest/doctest.h>

#include <cstdint>

namespace {

using Core::TimeRange;

constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC
constexpr std::int64_t HourUs = 3'600'000'000;

} // namespace

TEST_CASE("Zooming keeps the anchor in place")
{
    const TimeRange hour{ NoonUs, NoonUs + HourUs };

    CHECK(hour.zoomed(0.0, 0.5) == TimeRange{ NoonUs, NoonUs + HourUs / 2 });
    CHECK(hour.zoomed(1.0, 0.5) == TimeRange{ NoonUs + HourUs / 2, NoonUs + HourUs });
    CHECK(hour.zoomed(0.5, 2.0) == TimeRange{ NoonUs - HourUs / 2, NoonUs + HourUs * 3 / 2 });

    // Down to single microseconds, far from the epoch.
    const TimeRange tiny{ NoonUs, NoonUs + 10 };
    CHECK(tiny.zoomed(0.3, 0.5) == TimeRange{ NoonUs + 2, NoonUs + 7 });
}

TEST_CASE("Panning moves by a fraction of the length")
{
    const TimeRange hour{ NoonUs, NoonUs + HourUs };
    CHECK(hour.panned(0.25) == TimeRange{ NoonUs + HourUs / 4, NoonUs + HourUs * 5 / 4 });
    CHECK(hour.panned(-1.0) == TimeRange{ NoonUs - HourUs, NoonUs });
}
