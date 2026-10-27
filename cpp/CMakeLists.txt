cmake_minimum_required(VERSION 3.24)
project(
    TradeViewer
    VERSION 0.1.0
    LANGUAGES CXX C
)

include(cmake/common.cmake)

# Compile-time switch for timing (CPU scopes now, GPU timestamps later). OFF
# swaps in empty stubs; call sites don't change. CI builds both.
option(TRADEVIEWER_PROFILING "Compile in CPU and GPU timing" ON)

# Lets `ctest` find tests registered with viewer_add_test() in any subdirectory.
enable_testing()
include(cmake/testing.cmake)

add_subdirectory(modules/Core)
add_subdirectory(modules/Gpu)

add_subdirectory(apps/Viewer)
