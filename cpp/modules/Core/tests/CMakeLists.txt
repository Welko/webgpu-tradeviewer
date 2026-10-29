viewer_add_test(CoreTests
    LogTests.cpp
    ProfilingTests.cpp
    RingQueueTests.cpp
    SyntheticTests.cpp
    TradeParserTests.cpp
    TradesTests.cpp
)
target_link_libraries(CoreTests PRIVATE Core)
