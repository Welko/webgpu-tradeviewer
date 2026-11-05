#include "Core/Data/Instrument.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace Core::Data {

namespace {

// Quote assets a symbol without a separator may end with: stablecoins and
// fiat first, then the coins other coins are priced in.
constexpr std::array<std::string_view, 17> KnownQuotes{
    "USDT", "USDC", "FDUSD", "BUSD", "TUSD", "USDP", "DAI", "USD", "EUR", "GBP", "TRY", "BRL", "JPY", "AUD", "BTC", "ETH", "BNB",
};

bool isAlphanumeric(std::string_view text)
{
    return !text.empty() && std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c) != 0; });
}

bool hasLetter(std::string_view text)
{
    return std::ranges::any_of(text, [](unsigned char c) { return std::isalpha(c) != 0; });
}

std::string toUpper(std::string_view text)
{
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper;
}

} // namespace

std::optional<AssetNames> parseSymbol(std::string_view symbol)
{
    const std::string upper = toUpper(symbol);
    const std::string_view text(upper);
    if (const auto separator = text.find_first_of("-/_:"); separator != std::string_view::npos) {
        const auto base = text.substr(0, separator);
        auto quote = text.substr(separator + 1);
        quote = quote.substr(0, quote.find_first_of("-/_:"));
        if (!isAlphanumeric(base) || !isAlphanumeric(quote) || !hasLetter(base) || !hasLetter(quote)) {
            return std::nullopt;
        }
        return AssetNames{ .base = std::string(base), .quote = std::string(quote) };
    }

    std::string_view quote;
    for (const auto known : KnownQuotes) {
        if (text.size() > known.size() && text.ends_with(known) && known.size() > quote.size()) {
            quote = known;
        }
    }
    const auto base = text.substr(0, text.size() - quote.size());
    if (quote.empty() || !isAlphanumeric(base) || !hasLetter(base)) {
        return std::nullopt;
    }
    return AssetNames{ .base = std::string(base), .quote = std::string(quote) };
}

std::optional<AssetNames> findSymbolIn(std::string_view fileName)
{
    if (const auto folder = fileName.find_last_of("/\\"); folder != std::string_view::npos) {
        fileName.remove_prefix(folder + 1);
    }
    while (!fileName.empty()) {
        const auto end = fileName.find_first_of("-_.");
        if (auto names = parseSymbol(fileName.substr(0, end))) {
            return names;
        }
        if (end == std::string_view::npos) {
            break;
        }
        fileName.remove_prefix(end + 1);
    }
    return std::nullopt;
}

int countDecimals(std::string_view text)
{
    int exponent = 0;
    if (const auto e = text.find_first_of("eE"); e != std::string_view::npos) {
        const auto digits = text.substr(e + 1);
        std::from_chars(digits.data() + (digits.starts_with('+') ? 1 : 0), digits.data() + digits.size(), exponent);
        text = text.substr(0, e);
    }
    int fraction = 0;
    if (const auto point = text.find('.'); point != std::string_view::npos) {
        auto digits = text.substr(point + 1);
        while (digits.ends_with('0')) {
            digits.remove_suffix(1);
        }
        fraction = static_cast<int>(digits.size());
    }
    return std::max(0, fraction - exponent);
}

} // namespace Core::Data
