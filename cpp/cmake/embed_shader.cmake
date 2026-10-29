# Writes OUTPUT, a header defining INPUT's text as Gpu::Shaders::NAME.
# Run by gpu_embed_shader() (see shaders.cmake) with `cmake -P`.

file(READ "${INPUT}" source)
get_filename_component(fileName "${INPUT}" NAME)
if (source MATCHES "\\)wgsl\"")
    message(FATAL_ERROR "${INPUT} contains )wgsl\", which would end the embedded string early")
endif()

file(WRITE "${OUTPUT}" "// Generated from ${fileName} by gpu_embed_shader(). Edit that file instead.
#pragma once

#include <string_view>

namespace Gpu::Shaders {

inline constexpr std::string_view ${NAME} = R\"wgsl(${source})wgsl\";

} // namespace Gpu::Shaders
")
