#pragma once

#include "Gpu/Type.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace Gpu::Market {

/**
 * The public face of the Gpu module for market data. No WebGPU types appear
 * here. Builds on the generic Gpu::Engine, adding the market passes and the
 * data they draw.
 *
 * For now it only clears the surface. Trade and order-book passes will live
 * behind it.
 */
class Engine
{
public:
    using Result = std::expected<std::unique_ptr<Engine>, std::string>;
    using Callback = std::function<void(Result)>;

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

private:
    struct Impl;

    explicit Engine(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> mImpl;
};

} // namespace Gpu::Market
