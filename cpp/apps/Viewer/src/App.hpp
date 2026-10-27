#pragma once

#include "Gpu/Market/Engine.hpp"
#include "Gpu/Type.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace Viewer {

/**
 * The platform-independent application. The platform entry point
 * (NativeMain.cpp or WebMain.cpp) owns the loop: each iteration it calls
 * update(), then render() only if needsRender(), and otherwise sleeps until
 * something happens.
 */
class App
{
public:
    using Result = std::expected<std::unique_ptr<App>, std::string>;
    using Callback = std::function<void(Result)>;

    /** Calls `onDone` exactly once, possibly before returning. */
    static void create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone);

    explicit App(std::unique_ptr<Gpu::Market::Engine> engine) noexcept;

    /** The GPU in use. Each platform's UI decides how to show it. */
    [[nodiscard]] const Gpu::Type::Info& info() const;

    /** Surface size in physical pixels. */
    void resize(std::uint32_t width, std::uint32_t height);

    /** Advances application state to `nowMs`. */
    void update(double nowMs);

    /** Whether the current state differs from the last drawn frame. */
    [[nodiscard]] bool needsRender() const;

    /** Draws the current state. Does not change it. */
    void render();

    /** Turns frame timing on or off. Off by default. */
    void setProfiling(bool enabled);

    /** Stats of the last drawn frame. */
    [[nodiscard]] const Gpu::Type::FrameStats& stats() const;

#ifndef __EMSCRIPTEN__
    /** Delivers finished asynchronous GPU work. Call every loop iteration. */
    void processEvents();
#endif

private:
    std::unique_ptr<Gpu::Market::Engine> mEngine;
};

} // namespace Viewer
