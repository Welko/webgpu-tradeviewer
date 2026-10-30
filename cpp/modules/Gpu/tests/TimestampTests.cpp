#include "Gpu/Profiler.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("GPU timestamps count in ticks of the device's length")
{
    const std::vector<std::string> passes{ "grid", "trades" };
    // Begin and end of each pass.
    const std::array<std::uint64_t, 4> timestamps{ 100, 400, 400, 1'000'000 };

    // Nanoseconds already, as browsers report.
    const auto nanoseconds = Gpu::Profiler::toGpuTimes(passes, timestamps, 1.0);
    CHECK(nanoseconds.passes.size() == 2);
    if (nanoseconds.passes.size() == 2) {
        CHECK(nanoseconds.passes[0].name == "grid");
        CHECK(nanoseconds.passes[0].gpuMs == doctest::Approx(0.0003));
        CHECK(nanoseconds.passes[1].gpuMs == doctest::Approx(0.9996));
    }
    CHECK(nanoseconds.totalMs == doctest::Approx(0.9999));

    // Intel's Skylake GPUs tick every 83.33 ns.
    const auto ticks = Gpu::Profiler::toGpuTimes(passes, timestamps, 83.3333);
    if (ticks.passes.size() == 2) {
        CHECK(ticks.passes[0].gpuMs == doctest::Approx(0.025));
        CHECK(ticks.passes[1].gpuMs == doctest::Approx(83.3));
    }
}

TEST_CASE("A pass that ends before it begins takes no time")
{
    const std::array<std::uint64_t, 2> timestamps{ 500, 400 };
    const auto times = Gpu::Profiler::toGpuTimes({ "trades" }, timestamps, 1.0);
    CHECK(times.passes.size() == 1);
    CHECK(times.totalMs == 0.0);
}
