#include "Core/Plot/Axis.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>

namespace Core::Plot::Axis {

namespace {

/** Round time steps, in microseconds; each unit's own round numbers. */
// clang-format off: one row per unit.
constexpr std::array<std::int64_t, 36> TimeSteps{
    1, 2, 5, 10, 20, 50, 100, 200, 500,
    MillisecondUs, 2 * MillisecondUs, 5 * MillisecondUs, 10 * MillisecondUs, 20 * MillisecondUs, 50 * MillisecondUs,
    100 * MillisecondUs, 200 * MillisecondUs, 500 * MillisecondUs,
    SecondUs, 2 * SecondUs, 5 * SecondUs, 10 * SecondUs, 15 * SecondUs, 30 * SecondUs,
    MinuteUs, 2 * MinuteUs, 5 * MinuteUs, 10 * MinuteUs, 15 * MinuteUs, 30 * MinuteUs,
    HourUs, 2 * HourUs, 3 * HourUs, 6 * HourUs, 12 * HourUs,
    DayUs,
};
// clang-format on

std::string formatTime(std::int64_t timeUs, std::int64_t stepUs)
{
    using namespace std::chrono;
    const sys_time<microseconds> time{ microseconds(timeUs) };
    if (stepUs >= DayUs || timeUs % DayUs == 0)
    {
        return std::format("{:%F}", floor<days>(time));
    }
    if (stepUs >= MinuteUs)
    {
        return std::format("{:%H:%M}", floor<minutes>(time));
    }
    if (stepUs >= SecondUs)
    {
        return std::format("{:%T}", floor<seconds>(time));
    }
    if (stepUs >= MillisecondUs)
    {
        return std::format("{:%T}", floor<milliseconds>(time));
    }
    return std::format("{:%T}", time);
}

/** The smallest round step of at least `wanted` microseconds; whole days beyond the table. */
std::int64_t getTimeStep(std::int64_t wanted)
{
    for (const std::int64_t step : TimeSteps)
    {
        if (step >= wanted)
        {
            return step;
        }
    }
    return (wanted + DayUs - 1) / DayUs * DayUs;
}

/** 1, 2, 5 or 10: the smallest of them at least `mantissa`, which is between 1 and 10. */
double roundUp(double mantissa)
{
    for (const double round : { 1.0, 2.0, 5.0 })
    {
        if (mantissa <= round)
        {
            return round;
        }
    }
    return 10.0;
}

}  // namespace

std::vector<Tick> getTimeTicks(TimeRange range, std::size_t maxCount)
{
    const std::int64_t length = range.getLengthUs();
    if (length <= 0 || maxCount == 0)
    {
        return {};
    }
    // The smallest round step that keeps the count down.
    const std::int64_t wanted = (length + static_cast<std::int64_t>(maxCount) - 1) / static_cast<std::int64_t>(maxCount);
    const std::int64_t step = getTimeStep(wanted);

    // The first multiple of the step at or after the start, also before 1970.
    std::int64_t time = range.startUs / step * step;
    if (time < range.startUs)
    {
        time += step;
    }
    std::vector<Tick> ticks;
    for (; time < range.endUs; time += step)
    {
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
    if (!(span > 0.0) || maxCount == 0)
    {
        return {};
    }
    // 1, 2 or 5 times the power of ten just below the step that would fit maxCount.
    const double wanted = span / static_cast<double>(maxCount);
    const int exponent = static_cast<int>(std::floor(std::log10(wanted)));
    const double power = std::pow(10.0, exponent);
    const double mantissa = wanted / power;
    const double step = roundUp(mantissa) * power;
    const int decimals = std::max(0, -static_cast<int>(std::floor(std::log10(step) + 1e-9)));

    std::vector<Tick> ticks;
    // Counting whole steps avoids adding up rounding errors; the tolerance keeps
    // ticks that land exactly on an edge, such as 7540.70 / 0.01 = 754069.99999.
    constexpr double Tolerance = 1e-9;
    const auto first = static_cast<std::int64_t>(std::ceil(range.low / step - Tolerance));
    const auto last = static_cast<std::int64_t>(std::floor(range.high / step + Tolerance));
    for (std::int64_t index = first; index <= last; ++index)
    {
        const double price = static_cast<double>(index) * step;
        ticks.push_back({
            .fraction = (price - range.low) / span,
            .label = std::format("{:.{}f}", price, decimals),
        });
    }
    return ticks;
}

}  // namespace Core::Plot::Axis
