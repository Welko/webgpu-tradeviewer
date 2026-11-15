#pragma once

#include <format>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

/**
 * Two ways to report an error, never both for the same one:
 * - Return it, when something asked for the work that failed, such as
 *   loading a file. The caller knows what failed and decides what to show.
 * - Log it, when nothing asked, such as the GPU reporting an error or a
 *   frame's upload failing. onError takes it from there to the page.
 */
namespace Core::Log {

/** Called with every error logged, so a UI can show them too. Replaces the last one; empty for none. */
inline std::function<void(const std::string&)> onError;

/** Writes "<level>: <message>" and flushes. The format is checked at compile time. */
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
    std::string message = std::format(fmt, std::forward<Args>(args)...);
    std::cerr << "ERROR  : " << message << std::endl;
    if (onError) {
        onError(message);
    }
}

} // namespace Core::Log
