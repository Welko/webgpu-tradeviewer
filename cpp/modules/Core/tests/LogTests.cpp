#include "Core/Log.hpp"

#include <doctest/doctest.h>

#include <sstream>
#include <string>

TEST_CASE("Log writes the level, then the formatted message")
{
    std::ostringstream out;
    Core::Log::log(out, "INFO   ", "{} trades in {} ms", 42, 1.5);
    CHECK(out.str() == "INFO   : 42 trades in 1.5 ms\n");
}

TEST_CASE("Log accepts lvalue arguments and escaped braces")
{
    std::ostringstream out;
    const std::string symbol = "BTCUSDT";
    Core::Log::log(out, "WARNING", "{{{}}} has no trades", symbol);
    CHECK(out.str() == "WARNING: {BTCUSDT} has no trades\n");
}
