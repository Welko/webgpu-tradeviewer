#include "Core/Range.hpp"

#include <doctest/doctest.h>

#include <cstdint>

namespace {

using Core::DayUs;
using Core::HourUs;
using Core::TimeRange;

constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC

}  // namespace

TEST_CASE("Zooming keeps the anchor in place")
{
    const TimeRange hour{ .startUs = NoonUs, .endUs = NoonUs + HourUs };

    CHECK(hour.zoomed(0.0, 0.5) == TimeRange{ NoonUs, NoonUs + HourUs / 2 });
    CHECK(hour.zoomed(1.0, 0.5) == TimeRange{ NoonUs + HourUs / 2, NoonUs + HourUs });
    CHECK(hour.zoomed(0.5, 2.0) == TimeRange{ NoonUs - HourUs / 2, NoonUs + HourUs * 3 / 2 });

    // Down to single microseconds, far from the epoch.
    const TimeRange tiny{ .startUs = NoonUs, .endUs = NoonUs + 10 };
    CHECK(tiny.zoomed(0.3, 0.5) == TimeRange{ NoonUs + 2, NoonUs + 7 });
}

TEST_CASE("Panning moves by a fraction of the length")
{
    const TimeRange hour{ .startUs = NoonUs, .endUs = NoonUs + HourUs };
    CHECK(hour.panned(0.25) == TimeRange{ NoonUs + HourUs / 4, NoonUs + HourUs * 5 / 4 });
    CHECK(hour.panned(-1.0) == TimeRange{ NoonUs - HourUs, NoonUs });
}

TEST_CASE("A time's UTC day runs from its midnight to the next")
{
    const TimeRange day{ .startUs = NoonUs - 12 * HourUs, .endUs = NoonUs + 12 * HourUs };
    CHECK(Core::getUtcDay(NoonUs) == day);
    CHECK(Core::getUtcDay(day.startUs) == day);
    CHECK(Core::getUtcDay(day.endUs - 1) == day);
    CHECK(Core::getUtcDay(day.endUs) == TimeRange{ day.endUs, day.endUs + DayUs });

    // Before 1970 too, rounding down rather than toward zero.
    CHECK(Core::getUtcDay(0) == TimeRange{ 0, DayUs });
    CHECK(Core::getUtcDay(-1) == TimeRange{ -DayUs, 0 });
    CHECK(Core::getUtcDay(-DayUs) == TimeRange{ -DayUs, 0 });
}
