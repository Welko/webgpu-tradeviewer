#pragma once

#include "Core/Data/Instrument.hpp"
#include "Core/Data/Trades.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace Core::Data {

/**
 * Reads a trades CSV in chunks of any size, as it downloads or decompresses.
 * The first line tells the format apart:
 * - Tardis: a header row naming the columns, which may come in any order;
 *   timestamp, side, price and amount are used, and symbol if there is one.
 * - Binance public data: no header, and the columns id, price, qty,
 *   quoteQty, time, isBuyerMaker, isBestMatch. Time is in milliseconds
 *   (microseconds from 2025 on).
 *
 * The trades' instrument comes from the file too: its assets from the symbol
 * column, else from the file's name (see findSymbolIn()), else the
 * Instrument defaults; its decimals from the most any amount and price has.
 *
 * Stops at the first line it can't read. After an error, discard the parser.
 */
class TradeParser
{
public:
    /** `fileName` names the assets when the file doesn't, as Binance's don't: BTCUSDT-trades-2019-12-01.csv. */
    explicit TradeParser(std::string_view fileName = {});

    /** Parses each complete line of `chunk`; a line cut off at its end waits for the next chunk. */
    std::expected<void, std::string> append(std::string_view chunk);

    /** Parses the last line, if it had no newline, and hands over the trades in time order. */
    std::expected<Trades, std::string> finish();

    /** The memory held so far: the trades read, with room to grow, and the line waiting for its end. */
    [[nodiscard]] std::size_t getBytes() const { return mTrades.getBytes() + mPartialLine.capacity(); }

private:
    enum class Format : std::uint8_t
    {
        Unknown,
        Tardis,
        Binance,
    };

    /** Where the fields of a Tardis row are. */
    struct Columns
    {
        std::size_t count;
        std::size_t timestamp;
        std::size_t side;
        std::size_t price;
        std::size_t amount;
        std::size_t symbol;  // `count` without one
    };

    std::expected<void, std::string> parseLine(std::string_view line);
    std::expected<void, std::string> parseHeader(std::string_view line);
    std::expected<void, std::string> parseTrade(std::string_view line);
    [[nodiscard]] std::string formatError(std::string_view message) const;

    Format mFormat = Format::Unknown;
    Columns mColumns{};
    std::string mPartialLine;  // the end of the last chunk, without its newline yet
    std::size_t mLine = 0;  // lines read so far, for errors
    bool mSorted = true;
    Trades mTrades;
};

} // namespace Core::Data
