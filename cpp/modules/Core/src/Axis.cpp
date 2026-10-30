#include "Core/Axis.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>

namespace Core::Axis {

namespace {

constexpr std::int64_t Millisecond = 1'000;
constexpr std::int64_t Second = 1'000 * Millisecond;
constexpr std::int64_t Minute = 60 * Second;
constexpr std::int64_t Hour = 60 * Minute;
constexpr std::int64_t Day = 24 * Hour;

/** Round time steps, in microseconds; each unit's own round numbers. */
constexpr std::array<std::int64_t, 36> TimeSteps{
    1, 2, 5, 10, 20, 50, 100, 200, 500,
    Millisecond, 2 * Millisecond, 5 * Millisecond, 10 * Millisecond, 20 * Millisecond, 50 * Millisecond,
    100 * Millisecond, 200 * Millisecond, 500 * Millisecond,
    Second, 2 * Second, 5 * Second, 10 * Second, 15 * Second, 30 * Second,
    Minute, 2 * Minute, 5 * Minute, 10 * Minute, 15 * Minute, 30 * Minute,
    Hour, 2 * Hour, 3 * Hour, 6 * Hour, 12 * Hour,
    Day,
};

std::string formatTime(std::int64_t timeUs, std::int64_t stepUs)
{
    using namespace std::chrono;
    const sys_time<microseconds> time{ microseconds(timeUs) };
    if (stepUs >= Day || timeUs % Day == 0) {
        return std::format("{:%F}", floor<days>(time));
    }
    if (stepUs >= Minute) {
        return std::format("{:%H:%M}", floor<minutes>(time));
    }
    if (stepUs >= Second) {
        return std::format("{:%T}", floor<seconds>(time));
    }
    if (stepUs >= Millisecond) {
        return std::format("{:%T}", floor<milliseconds>(time));
    }
    return std::format("{:%T}", time);
}

} // namespace

std::vector<Tick> getTimeTicks(TimeRange range, std::size_t maxCount)
{
    const std::int64_t length = range.getLengthUs();
    if (length <= 0 || maxCount == 0) {
        return {};
    }
    // The smallest round step that keeps the count down; whole days beyond the table.
    const std::int64_t wanted = (length + static_cast<std::int64_t>(maxCount) - 1) / static_cast<std::int64_t>(maxCount);
    const auto found = std::ranges::find_if(TimeSteps, [&](std::int64_t step) { return step >= wanted; });
    const std::int64_t step = found != TimeSteps.end() ? *found : (wanted + Day - 1) / Day * Day;

    // The first multiple of the step at or after the start, also before 1970.
    std::int64_t time = range.startUs / step * step;
    if (time < range.startUs) {
        time += step;
    }
    std::vector<Tick> ticks;
    for (; time < range.endUs; time += step) {
        ticks.push_back({
            .fraction = static_cast<double>(time - range.startUs) / static_cast<double>(length),
            .label = formatTime(time, step),
        });
    }
    return ticks;
}

std::vector<Tick> getNumberTicks(ValueRange range, std::size_t maxCount)
{
    const double span = range.high - range.low;
    if (!(span > 0.0) || maxCount == 0) {
        return {};
    }
    // 1, 2 or 5 times the power of ten just below the step that would fit maxCount.
    const double wanted = span / static_cast<double>(maxCount);
    const int exponent = static_cast<int>(std::floor(std::log10(wanted)));
    const double power = std::pow(10.0, exponent);
    const double mantissa = wanted / power;
    const double step = (mantissa <= 1.0 ? 1.0 : mantissa <= 2.0 ? 2.0 : mantissa <= 5.0 ? 5.0 : 10.0) * power;
    const int decimals = std::max(0, -static_cast<int>(std::floor(std::log10(step) + 1e-9)));

    std::vector<Tick> ticks;
    // Counting whole steps avoids adding up rounding errors; the tolerance keeps
    // ticks that land exactly on an edge, such as 7540.70 / 0.01 = 754069.99999.
    constexpr double Tolerance = 1e-9;
    const auto first = static_cast<std::int64_t>(std::ceil(range.low / step - Tolerance));
    const auto last = static_cast<std::int64_t>(std::floor(range.high / step + Tolerance));
    for (std::int64_t index = first; index <= last; ++index) {
        const double price = static_cast<double>(index) * step;
        ticks.push_back({
            .fraction = (price - range.low) / span,
            .label = std::format("{:.{}f}", price, decimals),
        });
    }
    return ticks;
}

} // namespace Core::Axis
