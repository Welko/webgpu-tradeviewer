#include "Core/Axis.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using Core::Axis::Tick;

constexpr std::int64_t DayStartUs = 1'575'158'400'000'000;  // 2019-12-01 00:00 UTC
constexpr std::int64_t NoonUs = DayStartUs + 12 * 3'600'000'000LL;

std::vector<std::string> labelsOf(const std::vector<Tick>& ticks)
{
    std::vector<std::string> labels;
    for (const auto& tick : ticks) {
        labels.push_back(tick.label);
    }
    return labels;
}

} // namespace

TEST_CASE("Time ticks fall on round times, without the range's end")
{
    const auto ticks = Core::Axis::getTimeTicks({ NoonUs, NoonUs + 3'600'000'000 }, 6);
    CHECK(labelsOf(ticks) == std::vector<std::string>{ "12:00", "12:10", "12:20", "12:30", "12:40", "12:50" });
    CHECK(ticks.size() == 6);
    if (ticks.size() == 6) {
        CHECK(ticks[0].fraction == 0.0);
        CHECK(ticks[3].fraction == doctest::Approx(0.5));
    }
}

TEST_CASE("Time labels show the precision their step needs, and the date at midnight")
{
    CHECK(labelsOf(Core::Axis::getTimeTicks({ DayStartUs, DayStartUs + 86'400'000'000 }, 4))
          == std::vector<std::string>{ "2019-12-01", "06:00", "12:00", "18:00" });
    CHECK(labelsOf(Core::Axis::getTimeTicks({ NoonUs, NoonUs + 60'000'000 }, 4))
          == std::vector<std::string>{ "12:00:00", "12:00:15", "12:00:30", "12:00:45" });
    CHECK(labelsOf(Core::Axis::getTimeTicks({ NoonUs, NoonUs + 1'000'000 }, 2))
          == std::vector<std::string>{ "12:00:00.000", "12:00:00.500" });
    // Starting between ticks: the first is the next round time.
    CHECK(labelsOf(Core::Axis::getTimeTicks({ NoonUs + 1, NoonUs + 10 }, 5))
          == std::vector<std::string>{ "12:00:00.000002", "12:00:00.000004", "12:00:00.000006", "12:00:00.000008" });
    // Past the table of steps: whole days.
    CHECK(Core::Axis::getTimeTicks({ DayStartUs, DayStartUs + 10 * 86'400'000'000 }, 4).size() == 4);
}

TEST_CASE("Price ticks step by 1, 2 or 5 times a power of ten")
{
    CHECK(labelsOf(Core::Axis::getNumberTicks({ .low = 7500.3, .high = 7549.9 }, 5))
          == std::vector<std::string>{ "7510", "7520", "7530", "7540" });
    CHECK(labelsOf(Core::Axis::getNumberTicks({ .low = 7500.0, .high = 7600.0 }, 6))
          == std::vector<std::string>{ "7500", "7520", "7540", "7560", "7580", "7600" });
    // Cents, including ticks exactly on both edges.
    CHECK(labelsOf(Core::Axis::getNumberTicks({ .low = 7540.70, .high = 7540.75 }, 6))
          == std::vector<std::string>{ "7540.70", "7540.71", "7540.72", "7540.73", "7540.74", "7540.75" });
}

TEST_CASE("Empty ranges have no ticks")
{
    CHECK(Core::Axis::getTimeTicks({ NoonUs, NoonUs }, 5).empty());
    CHECK(Core::Axis::getTimeTicks({ NoonUs, NoonUs + 10 }, 0).empty());
    CHECK(Core::Axis::getNumberTicks({ .low = 7500.0, .high = 7500.0 }, 5).empty());
}
