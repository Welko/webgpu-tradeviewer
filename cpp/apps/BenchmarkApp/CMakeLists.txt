# Native only, and only with profiling compiled in: it reports the profiler's times.
add_executable(BenchmarkApp src/Main.cpp)
viewer_setup_target(BenchmarkApp)
# nlohmann/json comes from ViewerApp, which is added first.
target_link_libraries(BenchmarkApp PRIVATE ViewerAppLib nlohmann_json::nlohmann_json)
target_copy_webgpu_binaries(BenchmarkApp)
