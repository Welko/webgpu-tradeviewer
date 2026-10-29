cmake_minimum_required(VERSION 3.24)
project(
    TradeViewer
    VERSION 0.1.0
    LANGUAGES CXX C
)

include(cmake/common.cmake)
include(cmake/shaders.cmake)

# Native builds use Wayland or X11; other systems can come later.
if (NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Native builds support Linux only, for now")
endif()

option(TRADEVIEWER_PROFILING "Compile in CPU and GPU timing" ON)

# Function names cost about 17 KB gzipped and no speed. web/package.json sets
# this explicitly: on for `npm run dev`, off for the published site.
option(TRADEVIEWER_WASM_NAMES "Web: keep C++ function names in the wasm, for profilers and stack traces" OFF)

# Lets `ctest` find tests registered with viewer_add_test() in any subdirectory.
enable_testing()
include(cmake/testing.cmake)

add_subdirectory(modules/Core)
add_subdirectory(modules/Gpu)

add_subdirectory(apps/ViewerApp)
if (NOT EMSCRIPTEN)
    add_subdirectory(apps/TradeStatsApp)
endif()
