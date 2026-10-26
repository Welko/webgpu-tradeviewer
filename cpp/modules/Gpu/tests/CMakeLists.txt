# Tests of Gpu internals that need no GPU: they include headers from
# src/include directly and run in both presets (natively and in Node).
viewer_add_test(GpuTests
    HandleTests.cpp
)
target_include_directories(GpuTests PRIVATE ../src/include)
