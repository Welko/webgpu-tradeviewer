#include "Core/Data/TradeParser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
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

// Far longer than any trade's line: past this, it's not a trades file, and
// holding on to it could take as much memory as the file.
constexpr std::size_t MaxLineBytes = 64 * 1024;

// How much of a field an error message quotes.
constexpr std::size_t MaxQuotedBytes = 40;

// A byte order mark, which some editors put at the start of a text file.
constexpr std::string_view ByteOrderMark = "\xEF\xBB\xBF";

// The range times can be shown in: a browser's Date goes no further.
constexpr std::int64_t MaxTimeUs = 8'640'000'000'000'000'000;

// Beyond any market, and far inside what the GPU's 32-bit floats can draw.
constexpr double MaxValue = 1e18;
constexpr double MinPrice = 1e-18;

// Totals must fit in 64 bits as whole steps, with room to spare.
constexpr double MaxTotalUnits = 0x1p62;

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

/** `text`, safe to put in a message: short, and with bytes that aren't printable replaced. */
std::string quote(std::string_view text)
{
    std::string quoted(text.substr(0, MaxQuotedBytes));
    std::ranges::replace_if(quoted, [](unsigned char c) { return c < 0x20 || c >= 0x7f; }, '?');
    return std::format("\"{}{}\"", quoted, text.size() > MaxQuotedBytes ? "..." : "");
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

TradeParser::TradeParser(std::string_view fileName)
{
    auto names = findSymbolIn(fileName).value_or(AssetNames{});
    // The decimals grow to the most any line has.
    mTrades.instrument = { .base = std::move(names.base), .quote = std::move(names.quote), .amountDecimals = 0, .priceDecimals = 0 };
}

std::expected<void, std::string> TradeParser::append(std::string_view chunk)
{
    if (!mPartialLine.empty()) {
        const auto newline = chunk.find('\n');
        if (newline == std::string_view::npos) {
            mPartialLine += chunk;
            return checkPartialLine();
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
    return checkPartialLine();
}

std::expected<void, std::string> TradeParser::checkPartialLine() const
{
    if (mPartialLine.size() > MaxLineBytes) {
        return std::unexpected(std::format("Line {} is too long for a trades file", mLine + 1));
    }
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
    // As many decimals as let the total fit in 64 bits.
    double total = 0.0;
    for (const double amount : mTrades.amount) {
        total += amount;
    }
    auto& instrument = mTrades.instrument;
    while (instrument.amountDecimals > 0 && total * instrument.getUnitsPerWhole() >= MaxTotalUnits) {
        --instrument.amountDecimals;
    }
    if (total * instrument.getUnitsPerWhole() >= MaxTotalUnits) {
        return std::unexpected("The amounts add up to too much to count exactly");
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
    if (mLine == 1 && line.starts_with(ByteOrderMark)) {
        line.remove_prefix(ByteOrderMark.size());
    }
    if (line.empty()) {
        return {};
    }

    // Binance rows start with a trade id; anything else is a Tardis header.
    if (mFormat == Format::Unknown) {
        if (line.starts_with("PK\x03\x04") || line.starts_with("PK\x05\x06")) {
            return std::unexpected("This is a zip file: unzip it first");
        }
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
        return std::unexpected(formatError("too many columns"));
    }

    // Each column's index; `count` if it's missing.
    const auto column = [&](std::string_view name) {
        return static_cast<std::size_t>(std::ranges::find(fields.begin(), fields.begin() + count, name) - fields.begin());
    };
    for (const std::string_view name : { "timestamp", "side", "price", "amount" }) {
        if (column(name) == count) {
            return std::unexpected(formatError(std::format("no \"{}\" column", name)));
        }
    }
    mColumns = {
        .count = count,
        .timestamp = column("timestamp"),
        .side = column("side"),
        .price = column("price"),
        .amount = column("amount"),
        .symbol = column("symbol"),
    };
    return {};
}

std::expected<void, std::string> TradeParser::parseTrade(std::string_view line)
{
    Fields fields;
    const std::size_t count = split(line, fields);
    const std::size_t expected = mFormat == Format::Tardis ? mColumns.count : BinanceFields;
    if (count == 0) {
        return std::unexpected(formatError(std::format("expected {} fields, found more than {}", expected, MaxFields)));
    }
    if (count != expected) {
        return std::unexpected(formatError(std::format("expected {} fields, found {}", expected, count)));
    }

    const bool tardis = mFormat == Format::Tardis;
    const std::string_view timeText = fields[tardis ? mColumns.timestamp : 4];
    const std::string_view priceText = fields[tardis ? mColumns.price : 1];
    const std::string_view amountText = fields[tardis ? mColumns.amount : 2];
    const std::string_view sideText = fields[tardis ? mColumns.side : 5];

    auto timeUs = number<std::int64_t>(timeText);
    if (!timeUs) {
        return std::unexpected(formatError(std::format("the time {} isn't a whole number", quote(timeText))));
    }
    if (!tardis && *timeUs > -FirstMicrosecondTime && *timeUs < FirstMicrosecondTime) {
        *timeUs *= 1000;
    }
    if (*timeUs < -MaxTimeUs || *timeUs > MaxTimeUs) {
        return std::unexpected(formatError(std::format("the time {} is out of range", quote(timeText))));
    }

    const auto checkPositive = [&](std::string_view what, std::string_view text, const std::optional<double>& value) -> std::expected<void, std::string> {
        if (!value || !std::isfinite(*value) || *value <= 0.0) {
            return std::unexpected(formatError(std::format("the {} {} isn't a number above zero", what, quote(text))));
        }
        if (*value > MaxValue) {
            return std::unexpected(formatError(std::format("the {} {} is out of range", what, quote(text))));
        }
        return {};
    };
    const auto price = number<double>(priceText);
    if (auto checked = checkPositive("price", priceText, price); !checked) {
        return checked;
    }
    if (*price < MinPrice) {
        return std::unexpected(formatError(std::format("the price {} is out of range", quote(priceText))));
    }
    const auto amount = number<double>(amountText);
    if (auto checked = checkPositive("amount", amountText, amount); !checked) {
        return checked;
    }

    const auto side = tardis ? tardisSide(sideText) : binanceSide(sideText);
    if (!side) {
        return std::unexpected(formatError(tardis ? std::format("the side {} isn't buy or sell", quote(sideText))
                                                  : std::format("the buyer-maker flag {} isn't true or false", quote(sideText))));
    }

    // The symbol names the assets better than the file's name can.
    if (auto names = tardis && mTrades.empty() && mColumns.symbol < count ? parseSymbol(fields[mColumns.symbol]) : std::nullopt) {
        mTrades.instrument.base = std::move(names->base);
        mTrades.instrument.quote = std::move(names->quote);
    }
    if (!mTrades.empty() && *timeUs < mTrades.timeUs.back()) {
        mSorted = false;
    }
    auto& instrument = mTrades.instrument;
    instrument.amountDecimals = std::clamp(countDecimals(amountText), instrument.amountDecimals, Instrument::MaxDecimals);
    instrument.priceDecimals = std::clamp(countDecimals(priceText), instrument.priceDecimals, Instrument::MaxPriceDecimals);
    mTrades.timeUs.push_back(*timeUs);
    mTrades.price.push_back(*price);
    mTrades.amount.push_back(*amount);
    mTrades.side.push_back(*side);
    return {};
}

std::string TradeParser::formatError(std::string_view message) const
{
    return std::format("Line {}: {}", mLine, message);
}

} // namespace Core::Data
