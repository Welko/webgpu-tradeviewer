#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace Gpu::Type {

struct Info
{
    std::string vendor;
    std::string device;
    std::string architecture;
    std::string description;
    std::string backend;
    std::string adapter;

    /**
     * Web only: the renderer string WebGL reports, which browsers often
     * reveal when they hide the fields above. On multi-GPU machines it may
     * name a different GPU than the WebGPU adapter. Empty natively.
     */
    std::string webglRenderer;
};

/**
 * What the last drawn frame cost, and what the GPU holds. Plain data, so
 * each platform's UI can show it however it likes.
 */
struct FrameStats
{
    /** Frames drawn so far, counting this one. */
    std::uint64_t frame = 0;

    /**
     * Whether the CPU times below were measured: profiling is compiled in
     * and switched on. Otherwise they are 0.
     */
    bool timed = false;

    /** Waiting for and getting the surface image; includes vsync waits. */
    double cpuAcquireMs = 0.0;
    /** Recording every pass. */
    double cpuEncodeMs = 0.0;
    /** Finishing, submitting and presenting. */
    double cpuSubmitMs = 0.0;

    // Live GPU objects, from our own bookkeeping (WebGPU can't be asked).
    std::uint32_t buffers = 0;
    std::uint32_t textures = 0;
    std::uint64_t gpuBytes = 0;
};

/** A hint for choosing between GPUs. Browsers and drivers may ignore it. */
enum class PowerPreference
{
    Default,
    LowPower,
    HighPerformance,
};

/** Each platform compiles only its own targets. */
namespace Target
{

#ifdef __EMSCRIPTEN__

/** An HTML canvas, found with `document.querySelector(selector)`. */
struct Canvas
{
    std::string selector;
};

/** Where a Surface draws. Add alternatives here for new platforms. */
using Any = std::variant<Canvas>;

#else

/** A Wayland `wl_display*` and `wl_surface*`. */
struct Wayland
{
    void* display;
    void* surface;
};

/** An X11 `Display*` and `Window`. */
struct Xlib
{
    void* display;
    std::uint64_t window;
};

/**
 * A texture of this size instead of a window, so nothing is presented. For
 * headless tests: needs no display.
 */
struct Offscreen
{
    std::uint32_t width;
    std::uint32_t height;
};

/** Where a Surface draws. Add alternatives here for new platforms. */
using Any = std::variant<Wayland, Xlib, Offscreen>;

#endif

} // namespace Target

} // namespace Gpu::Type
