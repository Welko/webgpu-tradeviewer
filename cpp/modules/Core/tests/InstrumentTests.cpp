#include "Core/Data/Instrument.hpp"

#include <doctest/doctest.h>

#include <optional>

namespace {

using Core::Data::AssetNames;
using Core::Data::countDecimals;
using Core::Data::findSymbolIn;
using Core::Data::Instrument;
using Core::Data::parseSymbol;

} // namespace

TEST_CASE("A symbol splits at its separator, or else before the longest quote asset it ends with")
{
    CHECK(parseSymbol("BTCUSDT") == AssetNames{ .base = "BTC", .quote = "USDT" });
    CHECK(parseSymbol("ethbtc") == AssetNames{ .base = "ETH", .quote = "BTC" });
    CHECK(parseSymbol("BNBFDUSD") == AssetNames{ .base = "BNB", .quote = "FDUSD" });
    CHECK(parseSymbol("BTC-USD") == AssetNames{ .base = "BTC", .quote = "USD" });
    CHECK(parseSymbol("XBT/USD") == AssetNames{ .base = "XBT", .quote = "USD" });
    CHECK(parseSymbol("BTC-25DEC20-C") == AssetNames{ .base = "BTC", .quote = "25DEC20" });
    // Nothing that doesn't look like a pair.
    CHECK(parseSymbol("USDT") == std::nullopt);
    CHECK(parseSymbol("XBTUSDZ") == std::nullopt);
    CHECK(parseSymbol("2019") == std::nullopt);
    CHECK(parseSymbol("") == std::nullopt);
    CHECK(parseSymbol("BTC-") == std::nullopt);
}

TEST_CASE("A file's name gives its pair, as Binance and Tardis name their files")
{
    CHECK(findSymbolIn("BTCUSDT-trades-2019-12-01.zip") == AssetNames{ .base = "BTC", .quote = "USDT" });
    CHECK(findSymbolIn("../data/binance_trades_2019-12-01_ETHBTC.csv.gz") == AssetNames{ .base = "ETH", .quote = "BTC" });
    CHECK(findSymbolIn("C:\\Downloads\\LTCBUSD-trades-2019-12-01.csv") == AssetNames{ .base = "LTC", .quote = "BUSD" });
    CHECK(findSymbolIn("trades.csv.gz") == std::nullopt);
    CHECK(findSymbolIn("") == std::nullopt);
}

TEST_CASE("Decimals are counted as written, trailing zeros left out")
{
    CHECK(countDecimals("1.50") == 1);
    CHECK(countDecimals("0.03974100") == 6);
    CHECK(countDecimals("7540.78000000") == 2);
    CHECK(countDecimals("100") == 0);
    CHECK(countDecimals("100.000") == 0);
    CHECK(countDecimals("1e-5") == 5);
    CHECK(countDecimals("1.5e-3") == 4);
    CHECK(countDecimals("1.5E+2") == 0);
}

TEST_CASE("Amounts count in whole units of the instrument's finest step")
{
    const Instrument instrument{ .base = "BTC", .quote = "USDT", .amountDecimals = 6, .priceDecimals = 2 };
    CHECK(instrument.toAmountUnits(0.039741) == 39'741);
    CHECK(instrument.toAmountUnits(1.5) == 1'500'000);
    CHECK(instrument.toAmount(39'741) == 0.039741);
    // Without decimals, whole amounts.
    const Instrument whole{ .base = "SHIB", .quote = "USDT", .amountDecimals = 0, .priceDecimals = 8 };
    CHECK(whole.toAmountUnits(2'500'000.0) == 2'500'000);
    // Trades nobody described count in the finest step there is.
    CHECK(Instrument{}.amountDecimals == Instrument::MaxDecimals);
    CHECK(Instrument{}.base == "base");
}
