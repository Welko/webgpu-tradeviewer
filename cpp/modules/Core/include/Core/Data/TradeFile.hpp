#pragma once

#include "Core/Data/TradeParser.hpp"

#include <expected>
#include <string>

namespace Core::Data {

/**
 * Reads a trades CSV, gzip-compressed or not (see TradeParser, which `path`
 * names the assets for), with its warnings. Native only: the browser reads
 * its own files.
 */
std::expected<TradeParser::Finished, std::string> readTradeFile(const std::string& path);

} // namespace Core::Data
