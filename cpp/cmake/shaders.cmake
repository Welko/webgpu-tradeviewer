# Shaders live in .wgsl files next to the code that uses them. Each becomes a
# generated header holding it as a string, so binaries need no shader files at
# runtime. With naga (`cargo install naga-cli --version 29.0.4`, the version
# wgpu-native v29 uses), the build also checks each shader as it embeds it.

find_program(NAGA_EXECUTABLE naga HINTS "$ENV{HOME}/.cargo/bin")
option(TRADEVIEWER_REQUIRE_NAGA "Fail without naga, so every build checks its shaders" OFF)
if (NAGA_EXECUTABLE)
    message(STATUS "Checking shaders with ${NAGA_EXECUTABLE}")
elseif (TRADEVIEWER_REQUIRE_NAGA)
    message(FATAL_ERROR "naga not found; install it with `cargo install naga-cli --version 29.0.4`")
else()
    message(STATUS "naga not found: shaders are only checked when they're loaded")
endif()

set(EMBED_SHADER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/embed_shader.cmake")

# Embeds `shader`, a .wgsl file relative to the current source directory, into
# `target` as the header Gpu/Shaders/<name>.hpp, which defines
# `Gpu::Shaders::<name>` as a std::string_view.
function(gpu_embed_shader target shader)
    get_filename_component(name "${shader}" NAME_WE)
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/${shader}")
    set(includeDir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set(output "${includeDir}/Gpu/Shaders/${name}.hpp")

    set(check)
    if (NAGA_EXECUTABLE)
        set(check COMMAND "${NAGA_EXECUTABLE}" "${input}")
    endif()
    add_custom_command(
        OUTPUT "${output}"
        ${check}
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${input}" "-DOUTPUT=${output}" "-DNAME=${name}" -P "${EMBED_SHADER_SCRIPT}"
        DEPENDS "${input}" "${EMBED_SHADER_SCRIPT}"
        COMMENT "Embedding ${shader}"
        VERBATIM
    )
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${includeDir}")
endfunction()
