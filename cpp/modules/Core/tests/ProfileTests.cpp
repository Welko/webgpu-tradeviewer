#include "Core/Profile.hpp"

#include <doctest/doctest.h>

#include <chrono>

namespace {

// Busy-waits rather than sleeping, so it behaves the same natively and in Node.
void spin(std::chrono::milliseconds duration)
{
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
    }
}

} // namespace

TEST_CASE("An enabled scope writes the elapsed time when compiled in")
{
    double elapsedMs = -1.0;
    {
        const Core::Profile::Scope scope(elapsedMs, true);
        spin(std::chrono::milliseconds(2));
    }
    if constexpr (Core::Profile::Compiled) {
        CHECK(elapsedMs >= 2.0);
    } else {
        CHECK(elapsedMs == -1.0);
    }
}

TEST_CASE("A disabled scope leaves its target alone")
{
    double elapsedMs = -1.0;
    {
        const Core::Profile::Scope scope(elapsedMs, false);
        spin(std::chrono::milliseconds(1));
    }
    CHECK(elapsedMs == -1.0);
}
