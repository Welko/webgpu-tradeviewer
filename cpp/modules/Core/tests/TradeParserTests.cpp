#include "Core/Data/TradeParser.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Core::Data::Side;
using Core::Data::TradeParser;
using Core::Data::Trades;

constexpr std::string_view Tardis =
    "exchange,symbol,timestamp,local_timestamp,id,side,price,amount\n"
    "binance,BTCUSDT,1575158403572000,1575158403820370,211646077,sell,7540.78,0.039741\n"
    "binance,BTCUSDT,1575158403579000,1575158403820405,211646078,buy,7540.79,1.5\n";

constexpr std::string_view Binance =
    "211646077,7540.78000000,0.03974100,299.67,1575158403572,True,True\n"
    "211646078,7540.79000000,1.50000000,11311.18,1575158403579,False,True\n";

/** `text`, parsed in chunks of `chunkSize` bytes. */
std::expected<Trades, std::string> parse(std::string_view text, std::size_t chunkSize = std::string_view::npos)
{
    TradeParser parser;
    for (std::size_t offset = 0; offset < text.size(); offset += chunkSize) {
        if (auto appended = parser.append(text.substr(offset, chunkSize)); !appended) {
            return std::unexpected(appended.error());
        }
    }
    return parser.finish();
}

/** The two trades both samples hold. */
void checkSample(const std::expected<Trades, std::string>& trades)
{
    CHECK_MESSAGE(trades, (trades ? "" : trades.error()));
    if (!trades) {
        return;
    }
    CHECK(trades->timeUs == std::vector<std::int64_t>{ 1575158403572000, 1575158403579000 });
    CHECK(trades->price == std::vector<double>{ 7540.78, 7540.79 });
    CHECK(trades->amount == std::vector<double>{ 0.039741, 1.5 });
    CHECK(trades->side == std::vector<Side>{ Side::Sell, Side::Buy });
}

std::string errorOf(std::string_view text)
{
    const auto trades = parse(text);
    CHECK_FALSE(trades);
    return trades ? "" : trades.error();
}

} // namespace

TEST_CASE("Tardis trades are read by column name")
{
    checkSample(parse(Tardis));

    // The same trades, with the columns in another order.
    checkSample(parse("side,amount,price,timestamp\n"
                      "sell,0.039741,7540.78,1575158403572000\n"
                      "buy,1.5,7540.79,1575158403579000\n"));
}

TEST_CASE("Binance trades are read without a header, with times in milliseconds or microseconds")
{
    checkSample(parse(Binance));

    checkSample(parse("1,7540.78,0.039741,0,1575158403572000,True,True\n"
                      "2,7540.79,1.5,0,1575158403579000,False,True\n"));
}

TEST_CASE("Lines may be split across chunks anywhere")
{
    for (const std::string_view text : { Tardis, Binance }) {
        for (std::size_t chunkSize = 1; chunkSize <= text.size(); ++chunkSize) {
            CAPTURE(chunkSize);
            checkSample(parse(text, chunkSize));
        }
    }
}

TEST_CASE("Windows line endings and a missing final newline are fine")
{
    std::string text(Tardis);
    std::ranges::replace(text, '\n', '\r');
    for (auto position = text.find('\r'); position != std::string::npos; position = text.find('\r', position + 2)) {
        text.insert(position + 1, "\n");
    }
    text.resize(text.size() - 2);
    checkSample(parse(text));
}

TEST_CASE("Trades out of time order are sorted, keeping equal times in file order")
{
    const auto trades = parse("timestamp,side,price,amount\n"
                              "3,buy,1,1\n"
                              "1,buy,2,1\n"
                              "3,sell,3,1\n"
                              "2,buy,4,1\n");
    CHECK(trades);
    if (trades) {
        CHECK(trades->timeUs == std::vector<std::int64_t>{ 1, 2, 3, 3 });
        CHECK(trades->price == std::vector<double>{ 2, 4, 1, 3 });
        CHECK(trades->side == std::vector<Side>{ Side::Buy, Side::Buy, Side::Buy, Side::Sell });
    }
}

TEST_CASE("A file that can't be read gives the line and the reason")
{
    CHECK(errorOf("") == "The file is empty");
    CHECK(errorOf("exchange,symbol,timestamp,side,price,amount\n") == "The file has no trades");
    CHECK(errorOf("<html>\n") == "Line 1: no \"timestamp\" column");
    CHECK(errorOf("exchange,timestamp,side,amount\n") == "Line 1: no \"price\" column");
    CHECK(errorOf("exchange,timestamp,side,price,amount\nbinance,1,buy,2\n") == "Line 2: expected 5 fields");
    CHECK(errorOf("exchange,timestamp,side,price,amount\n\nbinance,1,hold,2,3\n")
          == "Line 3: can't read \"binance,1,hold,2,3\"");
    CHECK(errorOf("exchange,timestamp,side,price,amount\nbinance,1,buy,2,-3\n") == "Line 2: can't read \"binance,1,buy,2,-3\"");
    CHECK(errorOf("1,2.0,3.0,6.0,1575158403572,maybe,True\n") == "Line 1: can't read \"1,2.0,3.0,6.0,1575158403572,maybe,True\"");
}

TEST_CASE("A summary counts the trades, their time range and amount")
{
    const auto trades = parse(Tardis);
    CHECK(trades);
    if (trades) {
        const auto summary = Core::Data::getStats(*trades);
        CHECK(summary.count == 2);
        CHECK(summary.firstUs == 1575158403572000);
        CHECK(summary.lastUs == 1575158403579000);
        CHECK(summary.totalAmount == doctest::Approx(1.539741));
    }
    CHECK(Core::Data::getStats(Trades{}).count == 0);
}
