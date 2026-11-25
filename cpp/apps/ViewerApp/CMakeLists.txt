# App's parts (view, axes, summaries, pointer): Core only, no GPU, so they're
# tested on every platform, in Node too.
add_library(ViewerParts STATIC src/Axes.cpp src/Layout.cpp src/Pointer.cpp src/Summaries.cpp src/View.cpp)
viewer_setup_target(ViewerParts)
target_include_directories(ViewerParts PUBLIC include)
target_link_libraries(ViewerParts PUBLIC Core)

# The viewer without its platform (view state, input, engine), so BenchmarkApp
# can drive the same code.
add_library(ViewerAppLib STATIC src/App.cpp src/GpuSession.cpp src/Scene.cpp src/Telemetry.cpp)
viewer_setup_target(ViewerAppLib)
# What telemetry says it is.
target_compile_definitions(ViewerAppLib PRIVATE TRADEVIEWER_BUILD="${TRADEVIEWER_BUILD}" TRADEVIEWER_VERSION="${PROJECT_VERSION}")
target_link_libraries(ViewerAppLib PUBLIC ViewerParts Gpu)

add_executable(ViewerApp)
viewer_setup_target(ViewerApp)

target_link_libraries(ViewerApp PRIVATE ViewerAppLib)

if (EMSCRIPTEN)
    # The React app imports the generated module from web/src/wasm.
    set(VIEWER_WEB_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/../web/src/wasm" CACHE PATH
        "Where the Emscripten build writes ViewerApp.js and ViewerApp.wasm")

    target_sources(ViewerApp PRIVATE src/Web/Bridge.cpp src/Web/Convert.cpp src/Web/Main.cpp src/Web/Testing.cpp)
    target_include_directories(ViewerApp PRIVATE src)
    target_link_options(ViewerApp PRIVATE
        -lembind
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createViewerModule
        -sENVIRONMENT=web
        -sALLOW_MEMORY_GROWTH=1
        # The most wasm32 addresses, rather than the default 2 GB: a trade
        # takes about 80 bytes.
        -sMAXIMUM_MEMORY=4GB
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

    # stb_image_write, for --snapshot's PNGs. Header only.
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5
    )
    FetchContent_MakeAvailable(stb)

    # nlohmann/json, for --snapshot's JSON files. Header only.
    FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    )
    FetchContent_MakeAvailable(json)

    target_sources(ViewerApp PRIVATE src/Native/Main.cpp src/Native/Options.cpp src/Native/Png.cpp src/Native/Report.cpp
                                     src/Native/Snapshot.cpp src/Native/Window.cpp)
    target_include_directories(ViewerApp PRIVATE src)
    target_link_libraries(ViewerApp PRIVATE glfw nlohmann_json::nlohmann_json)
    target_include_directories(ViewerApp SYSTEM PRIVATE "${stb_SOURCE_DIR}")
    # For about.json, which says which wgpu-native drew the renderings.
    target_compile_definitions(ViewerApp PRIVATE TRADEVIEWER_WGPU_VERSION="${WGPU_VERSION}")

    # Expose the native handles of the platforms GLFW was built with.
    if (GLFW_BUILD_WAYLAND)
        target_compile_definitions(ViewerApp PRIVATE GLFW_EXPOSE_NATIVE_WAYLAND)
    endif()
    if (GLFW_BUILD_X11)
        target_compile_definitions(ViewerApp PRIVATE GLFW_EXPOSE_NATIVE_X11)
    endif()

    target_copy_webgpu_binaries(ViewerApp)
endif()

add_subdirectory(tests)
