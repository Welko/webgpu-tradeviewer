# No WebGPU dependency. Header-only until its first source file.
add_library(Core INTERFACE)

target_include_directories(Core INTERFACE include)
target_compile_features(Core INTERFACE cxx_std_23)
# Everything that links Core sees the same switch (0 or 1).
target_compile_definitions(Core INTERFACE TRADEVIEWER_PROFILING=$<BOOL:${TRADEVIEWER_PROFILING}>)

add_subdirectory(tests)
