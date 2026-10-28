include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

# Set in CI: each test then writes JUnit XML to <dir>/<build dir name>/<test>.xml
# instead of the console, for the run's summary.
set(TRADEVIEWER_JUNIT_DIR "$ENV{TRADEVIEWER_JUNIT_DIR}" CACHE PATH "Where tests write JUnit XML; empty for the console")
if (TRADEVIEWER_JUNIT_DIR)
    cmake_path(GET CMAKE_BINARY_DIR FILENAME buildName)
    set(junitDir "${TRADEVIEWER_JUNIT_DIR}/${buildName}")
    file(MAKE_DIRECTORY "${junitDir}")
endif()

## Builds a doctest executable from the given sources and registers it with
## ctest. Under Emscripten, ctest runs it in Node (the toolchain sets
## CMAKE_CROSSCOMPILING_EMULATOR), so keep GPU code out of tests.
function(viewer_add_test name)
    add_executable(${name} ${ARGN})
    viewer_setup_target(${name})
    target_link_libraries(${name} PRIVATE doctest::doctest_with_main)

    if (EMSCRIPTEN)
        target_link_options(${name} PRIVATE
            -sENVIRONMENT=node
            -sNODERAWFS=1        # read test fixtures straight from disk
            -sEXIT_RUNTIME=1     # report main's return code to ctest
            -sALLOW_MEMORY_GROWTH=1
        )
    endif()

    if (TRADEVIEWER_JUNIT_DIR)
        add_test(NAME ${name} COMMAND ${name} --reporters=junit "--out=${junitDir}/${name}.xml")
    else()
        add_test(NAME ${name} COMMAND ${name})
    endif()
endfunction()
