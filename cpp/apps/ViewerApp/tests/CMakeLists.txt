# The app's logic on a real GPU: natively only, like GpuDeviceTests. CI runs
# them on lavapipe.
viewer_add_test(ViewerAppTests AppTests.cpp SceneTests.cpp UnexpectedDataTests.cpp)
target_link_libraries(ViewerAppTests PRIVATE ViewerAppLib)
target_copy_webgpu_binaries(ViewerAppTests)
