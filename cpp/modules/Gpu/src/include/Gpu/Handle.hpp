#pragma once

#include "Gpu/Resource.hpp"

#include <cstdint>
#include <utility>

namespace Gpu {

/**
 * Owns one WebGPU object and releases it when destroyed (RAII).
 * Move-only: every object has exactly one owner.
 *
 * A tracked handle also reports its kind and size to a Resource::Stats until
 * it is released. That Resource::Stats must outlive the handle.
 */
template <typename T>
class Handle
{
public:
    /**
     * Specialized per WebGPU handle type (see Handles.hpp):
     * - `static void release(T)` releases the object;
     * - `static constexpr Resource::Kind Kind`, only for types
     *   Resource::Stats tracks.
     */
    struct Traits;

private:
    /**
     * Whether handles of T can report to Resource::Stats. A function, so it
     * is only evaluated once Traits is defined. Declared before the
     * constructors: Clang needs it before the requires-clause that uses it.
     */
    static constexpr bool tracked() { return requires { Traits::Kind; }; }

public:
    Handle() = default;

    /** Takes ownership of `raw`, which may be null. Not tracked. */
    explicit Handle(T raw) noexcept
        : mRaw(raw)
    {}

    /** Takes ownership of `raw` and reports `bytes` to `stats` while alive. */
    Handle(T raw, Resource::Stats& stats, std::uint64_t bytes) noexcept
        requires(tracked())
        : mRaw(raw),
          mStats(raw ? &stats : nullptr),
          mBytes(raw ? bytes : 0)
    {
        if (mStats) {
            mStats->add(Traits::Kind, mBytes);
        }
    }

    ~Handle()
    {
        reset();
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept
        : mRaw(std::exchange(other.mRaw, nullptr)),
          mStats(std::exchange(other.mStats, nullptr)),
          mBytes(std::exchange(other.mBytes, 0))
    {}

    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other) {
            reset();
            mRaw = std::exchange(other.mRaw, nullptr);
            mStats = std::exchange(other.mStats, nullptr);
            mBytes = std::exchange(other.mBytes, 0);
        }
        return *this;
    }

    [[nodiscard]] T get() const { return mRaw; }

    explicit operator bool() const { return mRaw != nullptr; }

    /** Releases the object now, if there is one, and becomes empty. */
    void reset() noexcept
    {
        if (!mRaw) {
            return;
        }
        if constexpr (tracked()) {
            if (mStats) {
                mStats->remove(Traits::Kind, mBytes);
            }
        }
        Traits::release(mRaw);
        mRaw = nullptr;
        mStats = nullptr;
        mBytes = 0;
    }

private:
    T mRaw = nullptr;
    Resource::Stats* mStats = nullptr;
    std::uint64_t mBytes = 0;
};

} // namespace Gpu
