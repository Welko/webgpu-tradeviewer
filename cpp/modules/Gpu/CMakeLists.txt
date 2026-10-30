include(FetchContent)

# Selects wgpu-native for native builds and emdawnwebgpu under Emscripten.
FetchContent_Declare(
    webgpu
    GIT_REPOSITORY https://github.com/eliemichel/WebGPU-distribution.git
    GIT_TAG 17dcd42a7683355e7a40ac4e97e77f36dff5b5ab
)
# Natively, wgpu-native v29 rather than WebGPU-distribution's default (v24):
# v29 says how long a GPU timestamp tick is, so GPU times can be converted to
# nanoseconds. Forced, so existing build folders switch too.
set(WGPU_VERSION "v29.0.1.1" CACHE STRING "wgpu-native release to download" FORCE)
FetchContent_MakeAvailable(webgpu)

add_library(Gpu STATIC
    src/Context.cpp
    src/GrowableBuffer.cpp
    src/Market/Engine.cpp
    src/Market/TradesPass.cpp
    src/Plot/GridPass.cpp
    src/Profiler.cpp
    src/Readback.cpp
)
viewer_setup_target(Gpu)
gpu_embed_shader(Gpu src/Market/TradesPass.wgsl)
gpu_embed_shader(Gpu src/Plot/GridPass.wgsl)

target_include_directories(Gpu PUBLIC include)
target_include_directories(Gpu PRIVATE src/include)
# Core's trade data appears in the public API.
target_link_libraries(Gpu PUBLIC Core)
target_link_libraries(Gpu PRIVATE webgpu)

add_subdirectory(tests)
