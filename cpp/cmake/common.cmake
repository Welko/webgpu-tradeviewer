## Configure common target properties and compile options
function(viewer_setup_target target)
    if(NOT TARGET ${target})
        message(WARNING "viewer_setup_target: target '${target}' does not exist")
        return()
    endif()

    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        COMPILE_WARNING_AS_ERROR ON
    )

    target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic)

    # No exceptions on any platform, as under Emscripten: nothing can throw,
    # and failures that would (e.g. out of memory) abort instead. Errors are
    # returned as std::expected.
    target_compile_options(${target} PRIVATE -fno-exceptions)
endfunction()
