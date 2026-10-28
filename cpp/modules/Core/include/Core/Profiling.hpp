#pragma once

#include <chrono>

// Set by CMake from the TRADEVIEWER_PROFILING option; on without it.
#ifndef TRADEVIEWER_PROFILING
#define TRADEVIEWER_PROFILING 1
#endif

namespace Core::Profiling {

/** Whether timing is compiled in. When false, Scope is an empty stub. */
inline constexpr bool Compiled = TRADEVIEWER_PROFILING != 0;

#if TRADEVIEWER_PROFILING

/**
 * Writes the CPU time from construction to destruction to `targetMs`. A
 * disabled scope reads no clock and leaves `targetMs` alone.
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

} // namespace Core::Profiling
