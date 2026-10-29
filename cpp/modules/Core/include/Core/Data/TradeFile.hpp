#pragma once

#include "Core/Data/Trades.hpp"

#include <expected>
#include <string>

namespace Core::Data {

/** Reads a trades CSV, gzip-compressed or not (see TradeParser). Native only: the browser reads its own files. */
std::expected<Trades, std::string> readTradeFile(const std::string& path);

} // namespace Core::Data
