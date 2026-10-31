// Prints how many trades a file has in a time range, and their total amount,
// computed the way the viewer computes what it shows. tools/check_ranges.py
// compares this with its own, independent calculation.
//
// Usage: TradeStatsApp <trades.csv[.gz]> <startUs> <endUs>
// Prints: <count> <amount>, the amount with exactly 8 decimals.

#include "Core/Data/TradeFile.hpp"
#include "Core/Data/Trades.hpp"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string_view>

namespace {

std::optional<std::int64_t> parseInt(std::string_view text)
{
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

int main(int argc, char** argv)
{
    const auto startUs = argc == 4 ? parseInt(argv[2]) : std::nullopt;
    const auto endUs = argc == 4 ? parseInt(argv[3]) : std::nullopt;
    if (!startUs || !endUs) {
        std::fputs("Usage: TradeStatsApp <trades.csv[.gz]> <startUs> <endUs>\n", stderr);
        return 2;
    }

    const auto trades = Core::Data::readTradeFile(argv[1]);
    if (!trades) {
        std::fputs(std::format("{}\n", trades.error()).c_str(), stderr);
        return 1;
    }

    const auto indices = Core::Data::getTradesIn(*trades, { .startUs = *startUs, .endUs = *endUs });
    const std::int64_t units = Core::Data::RunningTotals(*trades).getUnits(indices);
    constexpr std::int64_t UnitsPerWhole = 100'000'000;  // 1 / Core::Data::AmountUnit
    std::fputs(std::format("{} {}.{:08}\n", indices.size(), units / UnitsPerWhole, units % UnitsPerWhole).c_str(), stdout);
    return 0;
}
