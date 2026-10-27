#pragma once

#include <chrono>

// Set by CMake from the TRADEVIEWER_PROFILING option. Defaults to on when a
// target is built without it.
#ifndef TRADEVIEWER_PROFILING
#define TRADEVIEWER_PROFILING 1
#endif

namespace Core::Profile {

/**
 * Whether timing is compiled in. When false, Scope is an empty stub and
 * call sites stay unchanged.
 */
inline constexpr bool Compiled = TRADEVIEWER_PROFILING != 0;

#if TRADEVIEWER_PROFILING

/**
 * Measures the CPU time from construction to destruction and writes it to
 * `targetMs`, in milliseconds. A disabled scope reads no clock and leaves
 * `targetMs` alone, so the runtime switch costs one branch.
 */
class Scope
{
public:
    Scope(double& targetMs, bool enabled) noexcept
        : mTargetMs(enabled ? &targetMs : nullptr)
    {
        if (mTargetMs) {
            mStart = Clock::now();
        }
    }

    ~Scope()
    {
        if (mTargetMs) {
            *mTargetMs = std::chrono::duration<double, std::milli>(Clock::now() - mStart).count();
        }
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    using Clock = std::chrono::steady_clock;

    double* mTargetMs;
    Clock::time_point mStart{};
};

#else

class Scope
{
public:
    Scope(double& /*targetMs*/, bool /*enabled*/) noexcept {}

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

#endif

} // namespace Core::Profile
