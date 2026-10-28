#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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

// The stats structs below have no default values on purpose: build them
// with a designated initializer, and the compiler rejects any member left
// out (-Wmissing-field-initializers, an error in our builds). Use `{}` for
// an all-zero value.

/**
 * What one drawn frame cost, and what the GPU held. Plain data, so each
 * platform's UI can show it however it likes.
 */
struct FrameStats
{
    /** Where a frame's CPU time went. */
    struct Cpu
    {
        /** Waiting for and getting the surface image; includes vsync waits. */
        double acquireMs;
        /** Recording every pass. */
        double encodeMs;
        /** Finishing, submitting and presenting. */
        double submitMs;
    };

    /** How long a frame took on the GPU, measured with timestamp queries. */
    struct Gpu
    {
        /** How long one pass took on the GPU. */
        struct PassTiming
        {
            std::string name;
            double gpuMs;
        };

        double totalMs;
        std::vector<PassTiming> passes;
    };

    /** Why a frame has no GPU times. */
    enum class UntimedReason : std::uint8_t
    {
        Undefined,      /// Uninitialized state - indicates an error.
        ProfilingOff,   /// Profiling switched off at runtime or not compiled in.
        Unsupported,    /// timestamp-query not supported on this device.
        NoFreeSlot,     /// All slots are busy.
        NotStarted,     /// beginFrame() was never called.
        ReadbackFailed, /// The GPU readback failed.
        Overflow,       /// The frame was dropped due to overflow.
    };

    /** Which frame this is, counting from 1. */
    std::uint64_t frame;

    /** Set when profiling is compiled in and switched on. */
    std::optional<Cpu> cpu;

    /** The frame's GPU times, or why it has none. */
    std::expected<Gpu, UntimedReason> gpu;

    // Live GPU objects, from our own bookkeeping (WebGPU can't be asked).
    std::uint32_t buffers;
    std::uint32_t textures;
    std::uint64_t gpuBytes;
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

/** Where the Engine draws. Add alternatives here for new platforms. */
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

/** Where the Engine draws. Add alternatives here for new platforms. */
using Any = std::variant<Wayland, Xlib, Offscreen>;

#endif

} // namespace Target

} // namespace Gpu::Type
