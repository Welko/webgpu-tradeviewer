#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace Graphics::Type {

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

/** A hint for choosing between GPUs. Browsers and drivers may ignore it. */
enum class PowerPreference
{
    Default,
    LowPower,
    HighPerformance,
};

namespace Target
{

/** An HTML canvas, found with `document.querySelector(selector)`. Web only. */
struct Canvas
{
    std::string selector;
};

/** A Wayland `wl_display*` and `wl_surface*`. Native only. */
struct Wayland
{
    void* display;
    void* surface;
};

/** An X11 `Display*` and `Window`. Native only. */
struct Xlib
{
    void* display;
    std::uint64_t window;
};

/** Where the Context presents. Add alternatives here for new platforms. */
using Any = std::variant<Canvas, Wayland, Xlib>;

} // namespace Target

} // namespace Graphics::Type
