#include "Core/Data/TradeParser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <numeric>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace Core::Data {

namespace {

constexpr std::size_t MaxFields = 16;
constexpr std::size_t BinanceFields = 7;

// Any millisecond time before the year 5000 is below this, and any
// microsecond time after 1973 above it.
constexpr std::int64_t FirstMicrosecondTime = 100'000'000'000'000;

using Fields = std::array<std::string_view, MaxFields>;

/** Splits `line` at its commas. The number of fields, or 0 if there are too many. */
std::size_t split(std::string_view line, Fields& fields)
{
    for (std::size_t count = 0; count < fields.size(); ++count) {
        const auto comma = line.find(',');
        fields[count] = line.substr(0, comma);
        if (comma == std::string_view::npos) {
            return count + 1;
        }
        line.remove_prefix(comma + 1);
    }
    return 0;
}

/** `text` as a number, if all of it is one. */
template <typename T>
std::optional<T> number(std::string_view text)
{
    T value{};
    const char* end = text.data() + text.size();
    const auto [parsedEnd, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc{} || parsedEnd != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<Side> tardisSide(std::string_view text)
{
    if (text == "buy") {
        return Side::Buy;
    }
    if (text == "sell") {
        return Side::Sell;
    }
    return std::nullopt;
}

/** When the buyer's order was resting, the seller took liquidity. */
std::optional<Side> binanceSide(std::string_view isBuyerMaker)
{
    if (isBuyerMaker == "True" || isBuyerMaker == "true") {
        return Side::Sell;
    }
    if (isBuyerMaker == "False" || isBuyerMaker == "false") {
        return Side::Buy;
    }
    return std::nullopt;
}

/** Puts every column in time order, keeping trades at the same time in file order. */
void sortByTime(Trades& trades)
{
    std::vector<std::size_t> order(trades.size());
    std::ranges::iota(order, 0);
    std::ranges::stable_sort(order, {}, [&](std::size_t index) { return trades.timeUs[index]; });

    const auto reorder = [&order](auto& column) {
        auto sorted = column;
        for (std::size_t index = 0; index < order.size(); ++index) {
            sorted[index] = column[order[index]];
        }
        column = std::move(sorted);
    };
    reorder(trades.timeUs);
    reorder(trades.price);
    reorder(trades.amount);
    reorder(trades.side);
}

} // namespace

std::expected<void, std::string> TradeParser::append(std::string_view chunk)
{
    if (!mPartialLine.empty()) {
        const auto newline = chunk.find('\n');
        if (newline == std::string_view::npos) {
            mPartialLine += chunk;
            return {};
        }
        mPartialLine += chunk.substr(0, newline);
        chunk.remove_prefix(newline + 1);
        if (auto parsed = parseLine(mPartialLine); !parsed) {
            return parsed;
        }
        mPartialLine.clear();
    }

    for (auto newline = chunk.find('\n'); newline != std::string_view::npos; newline = chunk.find('\n')) {
        if (auto parsed = parseLine(chunk.substr(0, newline)); !parsed) {
            return parsed;
        }
        chunk.remove_prefix(newline + 1);
    }
    mPartialLine = chunk;
    return {};
}

std::expected<Trades, std::string> TradeParser::finish()
{
    if (!mPartialLine.empty()) {
        if (auto parsed = parseLine(mPartialLine); !parsed) {
            return std::unexpected(std::move(parsed.error()));
        }
        mPartialLine.clear();
    }
    if (mTrades.empty()) {
        return std::unexpected(mFormat == Format::Unknown ? "The file is empty" : "The file has no trades");
    }
    if (!mSorted) {
        sortByTime(mTrades);
    }
    return std::move(mTrades);
}

std::expected<void, std::string> TradeParser::parseLine(std::string_view line)
{
    ++mLine;
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return {};
    }

    // Binance rows start with a trade id; anything else is a Tardis header.
    if (mFormat == Format::Unknown) {
        if (line.front() < '0' || line.front() > '9') {
            mFormat = Format::Tardis;
            return parseHeader(line);
        }
        mFormat = Format::Binance;
    }
    return parseTrade(line);
}

std::expected<void, std::string> TradeParser::parseHeader(std::string_view line)
{
    Fields fields;
    const std::size_t count = split(line, fields);
    if (count == 0) {
        return std::unexpected(error("too many columns"));
    }

    // Each column's index; `count` if it's missing.
    const auto column = [&](std::string_view name) {
        return static_cast<std::size_t>(std::ranges::find(fields.begin(), fields.begin() + count, name) - fields.begin());
    };
    for (const std::string_view name : { "timestamp", "side", "price", "amount" }) {
        if (column(name) == count) {
            return std::unexpected(error(std::format("no \"{}\" column", name)));
        }
    }
    mColumns = {
        .count = count,
        .timestamp = column("timestamp"),
        .side = column("side"),
        .price = column("price"),
        .amount = column("amount"),
    };
    return {};
}

std::expected<void, std::string> TradeParser::parseTrade(std::string_view line)
{
    Fields fields;
    const std::size_t count = split(line, fields);
    const std::size_t expected = mFormat == Format::Tardis ? mColumns.count : BinanceFields;
    if (count != expected) {
        return std::unexpected(error(std::format("expected {} fields", expected)));
    }

    std::optional<std::int64_t> timeUs;
    std::optional<double> price;
    std::optional<double> amount;
    std::optional<Side> side;
    if (mFormat == Format::Tardis) {
        timeUs = number<std::int64_t>(fields[mColumns.timestamp]);
        price = number<double>(fields[mColumns.price]);
        amount = number<double>(fields[mColumns.amount]);
        side = tardisSide(fields[mColumns.side]);
    } else {
        timeUs = number<std::int64_t>(fields[4]);
        if (timeUs && *timeUs < FirstMicrosecondTime) {
            *timeUs *= 1000;
        }
        price = number<double>(fields[1]);
        amount = number<double>(fields[2]);
        side = binanceSide(fields[5]);
    }
    if (!timeUs || !price || !amount || !side || *price <= 0.0 || *amount <= 0.0) {
        return std::unexpected(error(std::format("can't read \"{}\"", line)));
    }

    if (!mTrades.empty() && *timeUs < mTrades.timeUs.back()) {
        mSorted = false;
    }
    mTrades.timeUs.push_back(*timeUs);
    mTrades.price.push_back(*price);
    mTrades.amount.push_back(*amount);
    mTrades.side.push_back(*side);
    return {};
}

std::string TradeParser::error(std::string_view message) const
{
    return std::format("Line {}: {}", mLine, message);
}

} // namespace Core::Data
