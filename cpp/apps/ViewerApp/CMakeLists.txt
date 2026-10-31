# The viewer without its platform (view state, input, engine), so BenchmarkApp
# can drive the same code.
add_library(ViewerAppLib STATIC src/App.cpp)
viewer_setup_target(ViewerAppLib)
target_include_directories(ViewerAppLib PUBLIC include)
target_link_libraries(ViewerAppLib PUBLIC Core Gpu)

add_executable(ViewerApp)
viewer_setup_target(ViewerApp)

target_link_libraries(ViewerApp PRIVATE ViewerAppLib)

if (EMSCRIPTEN)
    # The React app imports the generated module from web/src/wasm.
    set(VIEWER_WEB_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/../web/src/wasm" CACHE PATH
        "Where the Emscripten build writes ViewerApp.js and ViewerApp.wasm")

    target_sources(ViewerApp PRIVATE src/WebMain.cpp)
    target_link_options(ViewerApp PRIVATE
        -lembind
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createViewerModule
        -sENVIRONMENT=web
        -sALLOW_MEMORY_GROWTH=1
        --emit-tsd ViewerApp.d.ts
    )
    if (TRADEVIEWER_WASM_NAMES)
        # Names only; optimizations stay as they are, so profiles stay representative.
        target_link_options(ViewerApp PRIVATE --profiling-funcs)
    endif()
    set_target_properties(ViewerApp PROPERTIES
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${VIEWER_WEB_OUTPUT_DIR}"
    )
else()
    include(FetchContent)

    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )
    FetchContent_MakeAvailable(glfw)

    target_sources(ViewerApp PRIVATE src/NativeMain.cpp)
    target_link_libraries(ViewerApp PRIVATE glfw)

    # Expose the native handles of the platforms GLFW was built with.
    if (GLFW_BUILD_WAYLAND)
        target_compile_definitions(ViewerApp PRIVATE GLFW_EXPOSE_NATIVE_WAYLAND)
    endif()
    if (GLFW_BUILD_X11)
        target_compile_definitions(ViewerApp PRIVATE GLFW_EXPOSE_NATIVE_X11)
    endif()

    target_copy_webgpu_binaries(ViewerApp)

    add_subdirectory(tests)
endif()
