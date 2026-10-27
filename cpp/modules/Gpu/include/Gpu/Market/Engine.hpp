#pragma once

#include "Gpu/Type.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Gpu::Market {

/**
 * The public face of the Gpu module for market data: owns the WebGPU device,
 * where frames go, and the passes that draw them. No WebGPU types appear
 * here.
 *
 * For now it only clears the surface. Trade and order-book passes will live
 * behind it.
 */
class Engine
{
public:
    using Result = std::expected<std::unique_ptr<Engine>, std::string>;
    using Callback = std::function<void(Result)>;

    /** Tightly packed RGBA8 rows, top row first. */
    using Pixels = std::vector<std::uint8_t>;
    using PixelsCallback = std::function<void(std::expected<Pixels, std::string>)>;

    /**
     * Calls `onDone` exactly once: before returning (wgpu-native) or later
     * from the event loop (web).
     */
    static void create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone);

    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] const Gpu::Type::Info& info() const;

    /** Surface size in physical pixels. Zero sizes are ignored. */
    void resize(std::uint32_t width, std::uint32_t height);

    /**
     * Advances time-based state (animations, later the replay clock) to
     * `nowMs`. Call before deciding whether to render.
     */
    void update(double nowMs);

    /**
     * Whether a frame should be drawn: something changed since the last one,
     * or an animation is running. When false, the app can sleep until the
     * next input or resize.
     */
    [[nodiscard]] bool needsRender() const;

    /** Draws a frame, needed or not. */
    void render();

    /** Turns frame timing on or off. Off by default. */
    void setProfiling(bool enabled);

    /** Stats of the last drawn frame. */
    [[nodiscard]] const Gpu::Type::FrameStats& stats() const;

#ifndef __EMSCRIPTEN__
    /**
     * Delivers finished asynchronous WebGPU work, such as readPixels()
     * results. The native app calls it regularly; the browser does this on
     * its own.
     */
    void processEvents();

    /**
     * Copies the last drawn frame of an offscreen target back to the CPU,
     * e.g. for tests. Calls `onDone` exactly once, later, usually from
     * processEvents(). Fails for windows.
     */
    void readPixels(PixelsCallback onDone);
#endif

private:
    struct Impl;

    explicit Engine(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> mImpl;
};

} // namespace Gpu::Market
