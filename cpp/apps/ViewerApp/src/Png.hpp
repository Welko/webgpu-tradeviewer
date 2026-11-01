#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace Viewer::Png {

/** Writes RGBA rows, top row first, as a PNG file: losslessly, so its pixels read back exactly. */
[[nodiscard]] std::expected<void, std::string> write(const std::string& path, std::uint32_t widthPx, std::uint32_t heightPx,
                                                     std::span<const std::uint8_t> rgba);

} // namespace Viewer::Png
