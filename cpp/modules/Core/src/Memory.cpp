#include "Core/Memory.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/heap.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#endif

namespace Core::Memory {

std::optional<std::uint64_t> getProcessBytes()
{
#ifdef __EMSCRIPTEN__
    return emscripten_get_heap_size();
#elif defined(__linux__)
    // "<total pages> <resident pages> ...", read into the stack.
    const int file = open("/proc/self/statm", O_RDONLY);
    if (file < 0) {
        return std::nullopt;
    }
    char text[128] = {};
    const ssize_t read = ::read(file, text, sizeof(text) - 1);
    close(file);
    if (read <= 0) {
        return std::nullopt;
    }
    char* end = nullptr;
    std::strtoull(text, &end, 10);
    const std::uint64_t residentPages = std::strtoull(end, nullptr, 10);
    return residentPages * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#else
#error "getProcessBytes() knows Linux and Web only"
#endif
}

} // namespace Core::Memory
