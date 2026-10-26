#pragma once

#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace Core::Log {

template<typename... Args>
static void log(std::ostream& os, std::string_view level, std::string_view fmt, Args&&... args)
{
    try {
        const std::string message = std::vformat(fmt, std::make_format_args(args...));
        os << level << ": " << message << std::endl;
    } catch (const std::format_error& e) {
        os << level << ": (formatting error) " << e.what() << " -- fallback: ";
        os << fmt;
        ((os << ' ' << std::forward<Args>(args)), ...);
        os << std::endl;
    }
}

template<typename... Args>
static void info(std::string_view fmt, Args&&... args)
{
    log(std::cout, "INFO   ", fmt, std::forward<Args>(args)...);
}

template<typename... Args>
static void warn(std::string_view fmt, Args&&... args)
{
    log(std::cerr, "WARNING", fmt, std::forward<Args>(args)...);
}

template<typename... Args>
static void error(std::string_view fmt, Args&&... args)
{
    log(std::cerr, "ERROR  ", fmt, std::forward<Args>(args)...);
}

} // namespace Core::Log