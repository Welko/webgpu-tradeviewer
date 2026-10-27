viewer_add_test(CoreTests
    LogTests.cpp
    ProfileTests.cpp
)
target_link_libraries(CoreTests PRIVATE Core)
