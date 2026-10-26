cmake_minimum_required(VERSION 3.24)
project(
    TradeViewer
    VERSION 0.1.0
    LANGUAGES CXX C
)

include(cmake/common.cmake)

# Lets `ctest` find tests registered with viewer_add_test() in any subdirectory.
enable_testing()
include(cmake/testing.cmake)

add_subdirectory(modules/Core)
add_subdirectory(modules/Graphics)

add_subdirectory(apps/Viewer)
