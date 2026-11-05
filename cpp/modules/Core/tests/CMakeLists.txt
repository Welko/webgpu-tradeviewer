viewer_add_test(CoreTests
    AxisTests.cpp
    ColumnsTests.cpp
    InstrumentTests.cpp
    LogTests.cpp
    PickTests.cpp
    ProfilingTests.cpp
    RangeTests.cpp
    RingQueueTests.cpp
    SyntheticTests.cpp
    TradeParserTests.cpp
    TradesTests.cpp
    UnexpectedDataTests.cpp
)
target_link_libraries(CoreTests PRIVATE Core)

# Reading files, and making them to read: natively only, like readTradeFile().
if (NOT EMSCRIPTEN)
    target_sources(CoreTests PRIVATE TradeFileTests.cpp)
    target_link_libraries(CoreTests PRIVATE ZLIB::ZLIB)
endif()
