#include "Web/Bridge.hpp"

#include "Core/Log.hpp"

#include <emscripten/bind.h>
#include <emscripten/heap.h>

#include <cstdlib>
#include <string>

namespace Viewer::Web {

/**
 * Faults to inject, for the browser tests, as `Testing`: not part of the
 * viewer. They ship in the published build, so the tests run what's
 * published; each harms only the page that calls it.
 */
struct Testing
{
    /** Loses `viewer`'s GPU device, as a driver reset would. False before init() succeeds, or while the GPU restarts. */
    static bool loseGpu(Bridge& viewer)
    {
        return viewer.withApp([](App& app) { return app.loseGpu(); }, false);
    }

    /** Stops the module, as a bug would. */
    static void crash() { std::abort(); }

    /** Crashes inside the wasm code without aborting, as a memory bug would. */
    static void trap() { __builtin_trap(); }

    /** Logs an error, as the engine would when something fails. */
    static void logError(const std::string& message) { Core::Log::error("{}", message); }

    /** Asks for as much memory as the heap can ever have, which fails at once without using any. */
    static void exhaustMemory()
    {
        // Volatile, so the compiler can't leave out the request.
        char* volatile taken = new char[emscripten_get_heap_max()];
        delete[] taken;
    }
};

}  // namespace Viewer::Web

EMSCRIPTEN_BINDINGS(Testing)
{
    using Viewer::Web::Testing;
    emscripten::class_<Testing>("Testing")
        .class_function("loseGpu", &Testing::loseGpu)
        .class_function("crash", &Testing::crash)
        .class_function("trap", &Testing::trap)
        .class_function("logError", &Testing::logError)
        .class_function("exhaustMemory", &Testing::exhaustMemory);
}
