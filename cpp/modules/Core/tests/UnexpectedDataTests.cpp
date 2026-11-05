// Files that aren't what the parser expects: each must give a clear message
// or be read correctly, never a crash, nonsense numbers or a message nobody
// can read. Someone checking this program will try these.

#include "Core/Data/TradeParser.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>

namespace {

using Core::Data::TradeParser;
using Core::Data::Trades;

constexpr std::string_view Header = "timestamp,side,price,amount\n";

std::expected<Trades, std::string> parse(std::string_view text, std::size_t chunkSize = std::string_view::npos,
                                         std::string_view fileName = {})
{
    TradeParser parser(fileName);
    for (std::size_t offset = 0; offset < text.size(); offset += chunkSize) {
        if (auto appended = parser.append(text.substr(offset, chunkSize)); !appended) {
            return std::unexpected(appended.error());
        }
    }
    return parser.finish();
}

std::string errorOf(std::string_view text)
{
    const auto trades = parse(text);
    CHECK_FALSE(trades);
    return trades ? "" : trades.error();
}

/** One Tardis-style row after the header. */
std::string row(std::string_view time, std::string_view side, std::string_view price, std::string_view amount)
{
    return std::format("{}{},{},{},{}\n", Header, time, side, price, amount);
}

/** A message someone can read: short, and only printable text. */
void checkReadable(const std::string& message)
{
    CAPTURE(message);
    CHECK_FALSE(message.empty());
    CHECK(message.size() < 200);
    CHECK(std::ranges::all_of(message, [](unsigned char c) { return c >= 0x20 && c < 0x7f; }));
}

} // namespace

TEST_CASE("Prices and amounts must be real numbers above zero")
{
    for (const std::string_view text : { "nan", "NaN", "-nan", "inf", "-inf", "infinity", "0", "-1", "", "abc", "1.5x", "1,5" }) {
        CAPTURE(text);
        CHECK_FALSE(parse(row("1", "buy", text, "1")));
        CHECK_FALSE(parse(row("1", "buy", "1", text)));
    }
    CHECK(errorOf(row("1", "buy", "nan", "1")) == "Line 2: the price \"nan\" isn't a number above zero");
    CHECK(errorOf(row("1", "buy", "1", "inf")) == "Line 2: the amount \"inf\" isn't a number above zero");
    CHECK(errorOf("1,nan,1,0,1575158403572,True,True\n") == "Line 1: the price \"nan\" isn't a number above zero");
}

TEST_CASE("Numbers too extreme to draw or add up are refused")
{
    CHECK(errorOf(row("1", "buy", "1e300", "1")) == "Line 2: the price \"1e300\" is out of range");
    CHECK(errorOf(row("1", "buy", "1e-300", "1")) == "Line 2: the price \"1e-300\" is out of range");
    CHECK(errorOf(row("1", "buy", "1", "1e300")) == "Line 2: the amount \"1e300\" is out of range");
    // Times past what a browser's Date can show, or a 64-bit integer can hold.
    CHECK(errorOf(row("9000000000000000000", "buy", "1", "1")) == "Line 2: the time \"9000000000000000000\" is out of range");
    CHECK(errorOf(row("-9000000000000000000", "buy", "1", "1")) == "Line 2: the time \"-9000000000000000000\" is out of range");
    CHECK(errorOf("1,2.0,3.0,6.0,-9223372036854775808,True,True\n") == "Line 1: the time \"-9223372036854775808\" is out of range");
    CHECK(errorOf(row("99999999999999999999", "buy", "1", "1")) == "Line 2: the time \"99999999999999999999\" isn't a whole number");
    // Tiny amounts are fine: they round to nothing in the totals, and draw at the smallest size.
    CHECK(parse(row("1", "buy", "1", "1e-300")));
}

TEST_CASE("Times before 1970 are fine")
{
    const auto trades = parse(row("-1000000", "buy", "1", "1"));
    CHECK(trades);
    if (trades) {
        CHECK(trades->timeUs.front() == -1'000'000);
    }
}

TEST_CASE("Totals stay right when many decimals and large amounts don't fit in 64 bits together")
{
    // 10 decimals and 50 billion in all: 5·10^20 units, past 64 bits.
    std::string text(Header);
    for (int i = 0; i < 10; ++i) {
        text += std::format("{},buy,1,5000000000.1234567891\n", i);
    }
    const auto trades = parse(text);
    CHECK(trades);
    if (trades) {
        CHECK(Core::Data::getStats(*trades).totalAmount == doctest::Approx(50'000'000'001.23));
        CHECK(trades->instrument.amountDecimals < 10);
    }

    // Not even whole units fit.
    text = Header;
    for (int i = 0; i < 100; ++i) {
        text += std::format("{},buy,1,1e18\n", i);
    }
    CHECK(errorOf(text) == "The amounts add up to too much to count exactly");
}

TEST_CASE("More decimals than can be used are capped, for totals and for showing")
{
    const auto trades = parse(row("1", "buy", "0.123456789012345678901234567890", "0.123456789012345678901234567890"));
    CHECK(trades);
    if (trades) {
        CHECK(trades->instrument.amountDecimals == Core::Data::Instrument::MaxDecimals);
        CHECK(trades->instrument.priceDecimals <= 20);
    }
}

TEST_CASE("A byte order mark, as some editors add, is ignored")
{
    const std::string marked = "\xEF\xBB\xBF" + std::string(Header) + "1,buy,1,1\n";
    CHECK(parse(marked));
    CHECK(parse(marked, 1));
    CHECK(parse("\xEF\xBB\xBF" "1,2.0,3.0,6.0,1575158403572,True,True\n"));
}

TEST_CASE("Other files say what's wrong")
{
    // Another kind of market data: the order book file that comes with the trades.
    CHECK(errorOf("exchange,symbol,timestamp,local_timestamp,is_snapshot,side,price,amount\n"
                  "binance,BTCUSDT,1575158405045139,1575158405045139,true,ask,7541.38,0.085806\n")
          == "Line 2: the side \"ask\" isn't buy or sell");
    CHECK(errorOf("{\"trades\": []}\n") == "Line 1: no \"timestamp\" column");
    CHECK(errorOf("timestamp;side;price;amount\n1;buy;1;1\n") == "Line 1: no \"timestamp\" column");
    CHECK(errorOf("\"timestamp\",\"side\",\"price\",\"amount\"\n") == "Line 1: no \"timestamp\" column");
    CHECK(errorOf("timestamp, side, price, amount\n") == "Line 1: no \"side\" column");
    // A zip file opened natively, where only the browser unzips.
    CHECK(errorOf(std::string_view("PK\x03\x04\x14\0\0\0", 8)) == "This is a zip file: unzip it first");
    // A header in the middle, as from files joined together.
    CHECK(errorOf(row("1", "buy", "1", "1") + std::string(Header)) == "Line 3: the time \"timestamp\" isn't a whole number");
    CHECK(errorOf(row("1", "BUY", "1", "1")) == "Line 2: the side \"BUY\" isn't buy or sell");
    CHECK(errorOf("1,2.0,3.0,6.0,1575158403572,maybe,True\n") == "Line 1: the buyer-maker flag \"maybe\" isn't true or false");
    // Cut off in the middle of a line, as by a failed download.
    CHECK(errorOf(std::string(Header) + "1,buy,1") == "Line 2: expected 4 fields, found 3");
    CHECK(errorOf("\n\n\n") == "The file is empty");
}

TEST_CASE("Binary files and absurd lines give a short message people can read")
{
    std::string binary(1000, '\0');
    for (std::size_t i = 0; i < binary.size(); ++i) {
        binary[i] = static_cast<char>(i * 37 % 256);
    }
    checkReadable(errorOf(binary));

    // No line ends: this stops early, rather than holding the whole file in memory.
    const std::string endless(10'000'000, 'x');
    const auto chunked = parse(endless, 4096);
    CHECK_FALSE(chunked);
    if (!chunked) {
        checkReadable(chunked.error());
    }
    TradeParser parser;
    for (std::size_t offset = 0; offset < endless.size(); offset += 4096) {
        if (!parser.append(std::string_view(endless).substr(offset, 4096))) {
            break;
        }
    }
    CHECK(parser.getBytes() < 1'000'000);

    // A huge field, or bytes that aren't text.
    checkReadable(errorOf(row("1", std::string(100'000, 'x'), "1", "1")));
    checkReadable(errorOf(row("1", "b\xFFu\x01y", "1", "1")));
    checkReadable(errorOf(std::string(100'000, ',') + "\n"));
    checkReadable(errorOf("1" + std::string(100'000, 'x') + "\n"));
}
