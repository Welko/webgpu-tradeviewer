# Native only, and only with profiling compiled in: it reports the profiler's times.
add_executable(BenchmarkApp src/Main.cpp)
viewer_setup_target(BenchmarkApp)
target_link_libraries(BenchmarkApp PRIVATE ViewerAppLib)
target_copy_webgpu_binaries(BenchmarkApp)
