#include "Core/Data/TradeFile.hpp"

#include "Core/Data/TradeParser.hpp"

#include <zlib.h>

#include <array>
#include <format>
#include <memory>

namespace Core::Data {

std::expected<Trades, std::string> readTradeFile(const std::string& path)
{
    // gzopen() also reads files that aren't compressed.
    const std::unique_ptr<gzFile_s, decltype(&gzclose)> file(gzopen(path.c_str(), "rb"), &gzclose);
    if (!file) {
        return std::unexpected(std::format("Can't open {}", path));
    }
    TradeParser parser;
    std::array<char, 64 * 1024> buffer{};
    while (true) {
        const int read = gzread(file.get(), buffer.data(), static_cast<unsigned>(buffer.size()));
        if (read < 0) {
            int code = 0;
            return std::unexpected(std::format("Can't read {}: {}", path, gzerror(file.get(), &code)));
        }
        if (read == 0) {
            return parser.finish();
        }
        if (auto appended = parser.append({ buffer.data(), static_cast<std::size_t>(read) }); !appended) {
            return std::unexpected(std::format("{}: {}", path, appended.error()));
        }
    }
}

} // namespace Core::Data
