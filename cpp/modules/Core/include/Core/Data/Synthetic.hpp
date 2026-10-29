#pragma once

#include "Core/Data/Trades.hpp"

#include <cstdint>

namespace Core::Data {

/**
 * Made-up trades in [startUs, endUs): about 5 a second, at random times,
 * with a random-walk price near 7,500 and mostly small amounts. The same
 * seed gives the same trades.
 */
Trades generateSyntheticTrades(std::int64_t startUs, std::int64_t endUs, std::uint64_t seed);

} // namespace Core::Data
