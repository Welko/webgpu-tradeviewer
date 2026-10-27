# Tests of Gpu internals that need no GPU: they include headers from
# src/include directly and run in both presets (natively and in Node).
viewer_add_test(GpuTests
    HandleTests.cpp
    ResourceTests.cpp
)
target_include_directories(GpuTests PRIVATE ../src/include)

# Tests that drive a real GPU through the public API, natively only (Node has
# no WebGPU). In CI they run on Mesa's software Vulkan driver (lavapipe); they
# fail rather than skip when no GPU is available.
if(NOT EMSCRIPTEN)
    viewer_add_test(GpuDeviceTests
        EngineTests.cpp
    )
    target_link_libraries(GpuDeviceTests PRIVATE Gpu)
    target_copy_webgpu_binaries(GpuDeviceTests)
endif()
