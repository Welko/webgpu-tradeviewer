#include "Core/Data/TradeFile.hpp"

#include "Core/Data/TradeParser.hpp"

#include <zlib.h>

#include <array>
#include <format>
#include <memory>
#include <string_view>

namespace Core::Data {

std::expected<Trades, std::string> readTradeFile(const std::string& path)
{
    // gzopen() also reads files that aren't compressed.
    const std::unique_ptr<gzFile_s, decltype(&gzclose)> file(gzopen(path.c_str(), "rb"), &gzclose);
    if (!file) {
        return std::unexpected(std::format("Can't open {}", path));
    }
    TradeParser parser(path);
    std::array<char, 64 * 1024> buffer{};
    while (true) {
        const int read = gzread(file.get(), buffer.data(), static_cast<unsigned>(buffer.size()));
        // A file cut off shows only here: reading stops as if at its end.
        int code = Z_OK;
        std::string_view problem = gzerror(file.get(), &code);
        if (read < 0 || (read == 0 && code != Z_OK && code != Z_STREAM_END)) {
            // zlib starts its messages with the path.
            if (problem.starts_with(path + ": ")) {
                problem.remove_prefix(path.size() + 2);
            }
            return std::unexpected(std::format("Can't read {}: {}", path, problem));
        }
        if (read == 0) {
            auto trades = parser.finish();
            if (!trades) {
                return std::unexpected(std::format("{}: {}", path, trades.error()));
            }
            return trades;
        }
        if (auto appended = parser.append({ buffer.data(), static_cast<std::size_t>(read) }); !appended) {
            return std::unexpected(std::format("{}: {}", path, appended.error()));
        }
    }
}

} // namespace Core::Data
