# Tests of Gpu internals that need no GPU: they include headers from
# src/include directly and run in both presets (natively and in Node).
viewer_add_test(GpuTests
    HandleTests.cpp
    StyleTests.cpp
    ResourceTests.cpp
    StatsQueueTests.cpp
    TimestampTests.cpp
)
target_include_directories(GpuTests PRIVATE ../include ../src/include)
target_link_libraries(GpuTests PRIVATE Gpu Core webgpu)
target_copy_webgpu_binaries(GpuTests)

# Tests that need a real GPU, natively only (Node has no WebGPU). CI runs
# them on lavapipe; they fail rather than skip without a GPU.
if(NOT EMSCRIPTEN)
    viewer_add_test(GpuDeviceTests
        EngineTests.cpp
        ProfilerTests.cpp
    )
    target_include_directories(GpuDeviceTests PRIVATE ../src/include)
    target_link_libraries(GpuDeviceTests PRIVATE Gpu Core webgpu)
    target_copy_webgpu_binaries(GpuDeviceTests)
endif()
