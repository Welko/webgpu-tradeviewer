viewer_add_test(CoreTests
    AxisTests.cpp
    LogTests.cpp
    PickTests.cpp
    ProfilingTests.cpp
    RangeTests.cpp
    RingQueueTests.cpp
    SyntheticTests.cpp
    TradeParserTests.cpp
    TradesTests.cpp
)
target_link_libraries(CoreTests PRIVATE Core)
