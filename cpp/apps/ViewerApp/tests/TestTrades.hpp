#pragma once

#include "Core/Data/PreprocessedTrades.hpp"
#include "Core/Data/Trades.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

/** Small sets of trades for the parts' tests. */
namespace TestTrades {

inline constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC

/** `count` trades of amount 1, one every `spacingUs` from noon, trade i priced `price(i)`, buys and sells in turn. */
template<typename Price>
Core::Data::PreprocessedTrades make(std::size_t count, std::int64_t spacingUs, Price price)
{
    Core::Data::Trades trades;
    for (std::size_t i = 0; i < count; ++i)
    {
        trades.timeUs.push_back(NoonUs + (static_cast<std::int64_t>(i) * spacingUs));
        trades.price.push_back(price(i));
        trades.amount.push_back(1.0);
        trades.side.push_back(i % 2 == 0 ? Core::Data::Side::Buy : Core::Data::Side::Sell);
    }
    return Core::Data::PreprocessedTrades(std::move(trades));
}

}  // namespace TestTrades
