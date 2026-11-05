#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core::Data {

/**
 * What a file's trades are in: the asset bought and sold (the base, such as
 * BTC), the one prices are in (the quote, such as USDT), and how many
 * decimals amounts and prices come with. Read from the file, so nothing
 * assumes a market.
 */
struct Instrument
{
    /** The most decimals amounts are summed in, so a day's total stays far inside 64 bits. */
    static constexpr int MaxDecimals = 10;

    /** The most decimals prices are shown with: the parser refuses prices smaller than that. */
    static constexpr int MaxPriceDecimals = 18;

    std::string base = "base";
    std::string quote = "quote";
    // The most any amount or price in the file has, trailing zeros left out,
    // up to the limits above, and fewer if a file's total wouldn't fit in 64
    // bits. The most there can be for trades nobody described, such as those
    // made by hand.
    int amountDecimals = MaxDecimals;
    int priceDecimals = MaxDecimals;

    /** `amount` in whole units of the finest amount step: totals of these are exact, however they're added up. */
    [[nodiscard]] std::int64_t toAmountUnits(double amount) const { return std::llround(amount * getUnitsPerWhole()); }

    [[nodiscard]] double toAmount(std::int64_t units) const { return static_cast<double>(units) / getUnitsPerWhole(); }

    /** 10 to the amountDecimals, exactly. */
    [[nodiscard]] double getUnitsPerWhole() const { return PowersOfTen[static_cast<std::size_t>(amountDecimals)]; }

    bool operator==(const Instrument&) const = default;

private:
    // Each exact in a double.
    static constexpr std::array<double, MaxDecimals + 1> PowersOfTen{ 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10 };
};

/** An instrument's two assets, by name. */
struct AssetNames
{
    std::string base = "base";
    std::string quote = "quote";

    bool operator==(const AssetNames&) const = default;
};

/**
 * "BTCUSDT", "btc-usd" or "XBT/USD" as its two assets, in upper case; nothing
 * if it doesn't look like a pair. With a separator ("-", "/", "_" or ":"),
 * the parts on either side of the first one; without, the longest of a list
 * of common quote assets it ends with, and the rest.
 */
[[nodiscard]] std::optional<AssetNames> parseSymbol(std::string_view symbol);

/**
 * The pair a file's name gives, such as BTCUSDT-trades-2019-12-01.zip
 * (Binance) or binance_trades_2019-12-01_BTCUSDT.csv.gz (Tardis): the first
 * of its parts between "-", "_" and "." that parseSymbol() reads. Folders
 * are left out.
 */
[[nodiscard]] std::optional<AssetNames> findSymbolIn(std::string_view fileName);

/**
 * How many decimals the number `text` is written with, trailing zeros left
 * out: 1 for "1.50", 5 for "1e-5" and 0 for "100" or "1e3".
 */
[[nodiscard]] int countDecimals(std::string_view text);

} // namespace Core::Data
