# No WebGPU dependency.
add_library(Core STATIC
    src/Data/Synthetic.cpp
    src/Data/TradeParser.cpp
)
viewer_setup_target(Core)

target_include_directories(Core PUBLIC include)
target_compile_features(Core PUBLIC cxx_std_23)
# Everything that links Core sees the same switch (0 or 1).
target_compile_definitions(Core PUBLIC TRADEVIEWER_PROFILING=$<BOOL:${TRADEVIEWER_PROFILING}>)

add_subdirectory(tests)
