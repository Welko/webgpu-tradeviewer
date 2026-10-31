#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Gpu::Type {

/** A color as CSS gives it: sRGB, each channel from 0 to 1. */
struct Color
{
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

struct Info
{
    std::string vendor;
    std::string device;
    std::string architecture;
    std::string description;
    std::string backend;
    std::string adapter;

    /**
     * Web only: WebGL's renderer string, which often names the GPU when the
     * fields above are hidden. May name another GPU on multi-GPU machines.
     */
    std::string webglRenderer;
};

/** What one drawn frame cost, and what the GPU held. */
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

    /** How long a frame took on the GPU, from timestamp queries. */
    struct Gpu
    {
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
        Undefined,      ///< Not set yet.
        ProfilingOff,   ///< Switched off at runtime, or not compiled in.
        Unsupported,    ///< The device has no timestamp-query.
        NoFreeSlot,     ///< Every readback slot was busy.
        NotStarted,     ///< The profiler's beginFrame() wasn't called.
        ReadbackFailed, ///< The timestamps couldn't be read back.
        Overflow,       ///< Too many frames were waiting for their times.
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

/** Where the Engine draws. */
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

/** A texture instead of a window, for headless tests, in physical pixels. */
struct Offscreen
{
    std::uint32_t width;
    std::uint32_t height;
};

/** Where the Engine draws. */
using Any = std::variant<Wayland, Xlib, Offscreen>;

#endif

} // namespace Target

} // namespace Gpu::Type
