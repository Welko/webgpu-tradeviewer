add_executable(Viewer
    src/App.cpp
)
viewer_setup_target(Viewer)

target_link_libraries(Viewer PRIVATE
    Core
    Gpu
)

if (EMSCRIPTEN)
    # The React app imports the generated module from web/src/wasm.
    set(VIEWER_WEB_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/../web/src/wasm" CACHE PATH
        "Where the Emscripten build writes Viewer.js and Viewer.wasm")

    target_sources(Viewer PRIVATE src/WebMain.cpp)
    target_link_options(Viewer PRIVATE
        -lembind
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createViewerModule
        -sENVIRONMENT=web
        -sALLOW_MEMORY_GROWTH=1
        --emit-tsd Viewer.d.ts
    )
    set_target_properties(Viewer PROPERTIES
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

    # Reads .csv.gz trade files natively; the browser has DecompressionStream.
    find_package(ZLIB REQUIRED)

    target_sources(Viewer PRIVATE src/NativeMain.cpp)
    target_link_libraries(Viewer PRIVATE glfw ZLIB::ZLIB)

    # Expose the native handles of the platforms GLFW was built with.
    if (GLFW_BUILD_WAYLAND)
        target_compile_definitions(Viewer PRIVATE GLFW_EXPOSE_NATIVE_WAYLAND)
    endif()
    if (GLFW_BUILD_X11)
        target_compile_definitions(Viewer PRIVATE GLFW_EXPOSE_NATIVE_X11)
    endif()

    target_copy_webgpu_binaries(Viewer)
endif()
