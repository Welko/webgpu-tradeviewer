#include "Core/Data/Synthetic.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>

namespace {

constexpr std::int64_t HourUs = 3'600'000'000;
constexpr std::int64_t StartUs = 1'575'158'400'000'000;  // 2019-12-01 00:00 UTC

} // namespace

TEST_CASE("Synthetic trades fill the range plausibly, in time order")
{
    const auto trades = Core::Data::generateSyntheticTrades(StartUs, StartUs + HourUs, 1);

    // About 5 a second.
    CHECK(trades.size() > 17'000);
    CHECK(trades.size() < 19'000);
    CHECK(trades.timeUs.front() >= StartUs);
    CHECK(trades.timeUs.back() < StartUs + HourUs);
    CHECK(std::ranges::is_sorted(trades.timeUs));
    CHECK(std::ranges::all_of(trades.price, [](double price) { return price > 7000.0 && price < 8000.0; }));
    CHECK(std::ranges::all_of(trades.amount, [](double amount) { return amount > 0.0; }));
    CHECK(std::ranges::count(trades.side, Core::Data::Side::Buy) > 8'000);
    CHECK(std::ranges::count(trades.side, Core::Data::Side::Sell) > 8'000);
}

TEST_CASE("The same seed gives the same synthetic trades")
{
    const auto first = Core::Data::generateSyntheticTrades(StartUs, StartUs + HourUs, 1);
    const auto again = Core::Data::generateSyntheticTrades(StartUs, StartUs + HourUs, 1);
    const auto other = Core::Data::generateSyntheticTrades(StartUs, StartUs + HourUs, 2);
    CHECK(first.timeUs == again.timeUs);
    CHECK(first.price == again.price);
    CHECK(first.amount == again.amount);
    CHECK(first.side == again.side);
    CHECK(first.timeUs != other.timeUs);
}
