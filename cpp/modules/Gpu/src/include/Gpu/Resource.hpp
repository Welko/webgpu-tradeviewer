#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Gpu::Resource {

/** What a GPU object counts as in Stats. */
enum class Kind : std::uint8_t
{
    Buffer,
    Texture,
    QuerySet,
    Surface,
    Count,
};

/**
 * Live GPU objects and their bytes, by kind. This is our own bookkeeping:
 * WebGPU has no way to query allocations. Tracked handles add themselves on
 * creation and remove themselves on destruction, so the numbers are always
 * current.
 */
class Stats
{
public:
    void add(Kind kind, std::uint64_t bytes) noexcept
    {
        ++mCounts[index(kind)];
        mBytes[index(kind)] += bytes;
    }

    void remove(Kind kind, std::uint64_t bytes) noexcept
    {
        --mCounts[index(kind)];
        mBytes[index(kind)] -= bytes;
    }

    [[nodiscard]] std::uint32_t count(Kind kind) const { return mCounts[index(kind)]; }
    [[nodiscard]] std::uint64_t bytes(Kind kind) const { return mBytes[index(kind)]; }

    [[nodiscard]] std::uint64_t totalBytes() const
    {
        std::uint64_t total = 0;
        for (const auto bytes : mBytes) {
            total += bytes;
        }
        return total;
    }

    /** Whether nothing is alive. */
    [[nodiscard]] bool empty() const
    {
        for (const auto count : mCounts) {
            if (count > 0) {
                return false;
            }
        }
        return true;
    }

    /** What is alive, e.g. "surfaces: 1, textures: 2 (2048 bytes)". */
    [[nodiscard]] std::string describe() const
    {
        auto name = [](Kind kind) {
            switch (kind) {
            case Kind::Buffer: return "buffers";
            case Kind::Texture: return "textures";
            case Kind::QuerySet: return "query sets";
            case Kind::Surface: return "surfaces";
            case Kind::Count: break;
            }
            return "unknown";
        };

        std::string result;
        for (std::size_t i = 0; i < KindCount; ++i) {
            if (mCounts[i] == 0) {
                continue;
            }
            if (!result.empty()) {
                result += ", ";
            }
            result += std::format("{}: {}", name(static_cast<Kind>(i)), mCounts[i]);
            if (mBytes[i] > 0) {
                result += std::format(" ({} bytes)", mBytes[i]);
            }
        }
        return result.empty() ? "nothing" : result;
    }

private:
    static constexpr std::size_t KindCount = static_cast<std::size_t>(Kind::Count);

    static constexpr std::size_t index(Kind kind) { return static_cast<std::size_t>(kind); }

    std::array<std::uint32_t, KindCount> mCounts{};
    std::array<std::uint64_t, KindCount> mBytes{};
};

} // namespace Gpu::Resource
