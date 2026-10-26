#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Gpu::Resource {

/** What a tracked handle counts as in Stats. */
enum class Kind : std::uint8_t
{
    Buffer,
    Texture,
    QuerySet,
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
    void add(Kind kind, std::uint64_t bytes)
    {
        ++mCounts[index(kind)];
        mBytes[index(kind)] += bytes;
    }

    void remove(Kind kind, std::uint64_t bytes)
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

private:
    static constexpr std::size_t KindCount = static_cast<std::size_t>(Kind::Count);

    static constexpr std::size_t index(Kind kind) { return static_cast<std::size_t>(kind); }

    std::array<std::uint32_t, KindCount> mCounts{};
    std::array<std::uint64_t, KindCount> mBytes{};
};

} // namespace Gpu::Resource
