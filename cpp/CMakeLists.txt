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
option(TRADEVIEWER_TELEMETRY "Compile in anonymous telemetry, sent only with the user's consent" ON)

# The commit built, which telemetry and crash reports name: CI's, else the
# checked-out one when CMake configures. A rebuild without configuring keeps it.
if (DEFINED ENV{GITHUB_SHA})
    string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 TRADEVIEWER_BUILD_DEFAULT)
else()
    execute_process(COMMAND git rev-parse --short=7 HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                    OUTPUT_VARIABLE TRADEVIEWER_BUILD_DEFAULT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if (NOT TRADEVIEWER_BUILD_DEFAULT)
    set(TRADEVIEWER_BUILD_DEFAULT "unknown")
endif()
set(TRADEVIEWER_BUILD "${TRADEVIEWER_BUILD_DEFAULT}" CACHE STRING "The commit built, as telemetry names it" FORCE)

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
    if (TRADEVIEWER_PROFILING)
        add_subdirectory(apps/BenchmarkApp)
    endif()
endif()
