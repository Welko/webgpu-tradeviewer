#pragma once

#include <cstdint>

namespace Viewer {

/**
 * How the benchmarks measure: BenchmarkApp natively, and web/src/benchmark.ts,
 * which reads these from the wasm module, so their tables read side by side.
 */
struct BenchmarkSettings
{
    std::uint32_t widthPx;
    std::uint32_t heightPx;
    std::uint32_t warmUpRuns;  // per view, not measured
    std::uint32_t measuredRuns;  // per view
};

// Task 3's viewport, at a pixel ratio of 1.
inline constexpr BenchmarkSettings Benchmark{ .widthPx = 1600, .heightPx = 900, .warmUpRuns = 10, .measuredRuns = 60 };

}  // namespace Viewer
