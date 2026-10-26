# No WebGPU dependency. Header-only for now; switch to STATIC with the first
# source file (e.g. the trade parser under include/Core/Data).
add_library(Core INTERFACE)

target_include_directories(Core INTERFACE include)
target_compile_features(Core INTERFACE cxx_std_23)
