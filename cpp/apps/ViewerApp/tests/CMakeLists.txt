# App's parts without a GPU: on every platform.
viewer_add_test(ViewerPartsTests AxesTests.cpp PointerTests.cpp SummariesTests.cpp ViewTests.cpp)
target_link_libraries(ViewerPartsTests PRIVATE ViewerParts)

# The app's logic on a real GPU: natively only, like GpuDeviceTests. CI runs
# them on lavapipe.
if (NOT EMSCRIPTEN)
    viewer_add_test(ViewerAppTests AppTests.cpp GpuSessionTests.cpp SceneTests.cpp UnexpectedDataTests.cpp)
    target_link_libraries(ViewerAppTests PRIVATE ViewerAppLib)
    target_copy_webgpu_binaries(ViewerAppTests)
endif()
