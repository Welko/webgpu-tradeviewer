add_executable(TradeStatsApp src/Main.cpp)
viewer_setup_target(TradeStatsApp)
target_link_libraries(TradeStatsApp PRIVATE Core)
