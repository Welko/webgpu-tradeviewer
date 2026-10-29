include(FetchContent)

# Selects wgpu-native for native builds and emdawnwebgpu under Emscripten.
FetchContent_Declare(
    webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
    GIT_TAG 17dcd42a7683355e7a40ac4e97e77f36dff5b5ab
)
FetchContent_MakeAvailable(webgpu)

add_library(Gpu STATIC
    src/Context.cpp
    src/GrowableBuffer.cpp
    src/Market/Engine.cpp
    src/Market/TradesPass.cpp
    src/Profiler.cpp
    src/Readback.cpp
)
viewer_setup_target(Gpu)

target_include_directories(Gpu PUBLIC include)
target_include_directories(Gpu PRIVATE src/include)
# Core's trade data appears in the public API.
target_link_libraries(Gpu PUBLIC Core)
target_link_libraries(Gpu PRIVATE webgpu)

add_subdirectory(tests)
