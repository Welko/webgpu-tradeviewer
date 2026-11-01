#include "Png.hpp"

#include <format>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace Viewer::Png {

std::expected<void, std::string> write(const std::string& path, std::uint32_t widthPx, std::uint32_t heightPx,
                                       std::span<const std::uint8_t> rgba)
{
    const std::size_t rowBytes = std::size_t{ widthPx } * 4;
    if (widthPx == 0 || heightPx == 0 || rgba.size() != rowBytes * heightPx) {
        return std::unexpected(std::format("{} bytes aren't {} × {} RGBA pixels", rgba.size(), widthPx, heightPx));
    }
    if (stbi_write_png(path.c_str(), static_cast<int>(widthPx), static_cast<int>(heightPx), 4, rgba.data(),
                       static_cast<int>(rowBytes)) == 0) {
        return std::unexpected(std::format("Failed to write {}", path));
    }
    return {};
}

} // namespace Viewer::Png
