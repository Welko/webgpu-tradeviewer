#include "Png.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <fstream>
#include <string_view>
#include <vector>

namespace Viewer::Png {

namespace {

using Bytes = std::vector<std::uint8_t>;

// Deflate's stored blocks hold up to this many bytes each.
constexpr std::size_t MaxStoredBlock = 65'535;

void appendBigEndian(Bytes& out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes)
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> entries{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            entries[n] = c;
        }
        return entries;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::uint8_t byte : bytes) {
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t adler32(std::span<const std::uint8_t> bytes)
{
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : bytes) {
        a = (a + byte) % 65'521;
        b = (b + a) % 65'521;
    }
    return (b << 16) | a;
}

/** A chunk: length, type, data, and the CRC of type and data. */
void appendChunk(Bytes& out, std::string_view type, std::span<const std::uint8_t> data)
{
    appendBigEndian(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeStart = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), data.begin(), data.end());
    appendBigEndian(out, crc32(std::span(out).subspan(typeStart)));
}

/** A zlib stream of stored (uncompressed) deflate blocks. */
Bytes storeInZlib(std::span<const std::uint8_t> data)
{
    Bytes out{ 0x78, 0x01 };
    std::size_t offset = 0;
    do {
        const std::size_t size = std::min(MaxStoredBlock, data.size() - offset);
        const bool last = offset + size == data.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(size));
        out.push_back(static_cast<std::uint8_t>(size >> 8));
        out.push_back(static_cast<std::uint8_t>(~size));
        out.push_back(static_cast<std::uint8_t>(~size >> 8));
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                   data.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
    } while (offset < data.size());
    appendBigEndian(out, adler32(data));
    return out;
}

} // namespace

std::expected<void, std::string> write(const std::string& path, std::uint32_t widthPx, std::uint32_t heightPx,
                                       std::span<const std::uint8_t> rgba)
{
    const std::size_t rowBytes = std::size_t{ widthPx } * 4;
    if (widthPx == 0 || heightPx == 0 || rgba.size() != rowBytes * heightPx) {
        return std::unexpected(std::format("{} bytes aren't {} × {} RGBA pixels", rgba.size(), widthPx, heightPx));
    }

    // Each row starts with its filter type: 0, none.
    Bytes rows;
    rows.reserve((rowBytes + 1) * heightPx);
    for (std::uint32_t row = 0; row < heightPx; ++row) {
        rows.push_back(0);
        const auto start = rgba.begin() + static_cast<std::ptrdiff_t>(row * rowBytes);
        rows.insert(rows.end(), start, start + static_cast<std::ptrdiff_t>(rowBytes));
    }

    Bytes header;
    appendBigEndian(header, widthPx);
    appendBigEndian(header, heightPx);
    header.insert(header.end(), { 8, 6, 0, 0, 0 });  // 8 bits per channel, RGBA, deflate, adaptive filters, no interlacing

    Bytes file{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    appendChunk(file, "IHDR", header);
    appendChunk(file, "IDAT", storeInZlib(rows));
    appendChunk(file, "IEND", {});

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!out) {
        return std::unexpected(std::format("Failed to write {}", path));
    }
    return {};
}

} // namespace Viewer::Png
