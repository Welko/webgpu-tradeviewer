#pragma once

#include <format>
#include <iostream>
#include <string_view>
#include <utility>

namespace Core::Log {

/**
 * Writes "<level>: <message>" and flushes.
 *
 * The format string is checked at compile time, so a mismatch is a build
 * error rather than a runtime exception.
 */
template<typename... Args>
void log(std::ostream& os, std::string_view level, std::format_string<Args...> fmt, Args&&... args)
{
    os << level << ": " << std::format(fmt, std::forward<Args>(args)...) << std::endl;
}

template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args)
{
    log(std::cout, "INFO   ", fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args)
{
    log(std::cerr, "WARNING", fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args)
{
    log(std::cerr, "ERROR  ", fmt, std::forward<Args>(args)...);
}

} // namespace Core::Log
