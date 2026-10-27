#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/Pass.hpp"
#include "Gpu/Surface.hpp"
#include "Gpu/Type.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Gpu {

/**
 * Internal to Gpu: the generic core that domain facades such as
 * Gpu::Market::Engine build on. Owns the context, the surface and the
 * passes, and draws frames by running the passes in order. Knows nothing
 * about what the passes draw.
 *
 * Renders on demand: anything that changes what a frame would show marks
 * the engine dirty, and the app's loop draws only while needsRender().
 */
class Engine
{
public:
    using Result = std::expected<Engine, std::string>;
    using Callback = std::function<void(Result)>;

    /**
     * Creates the context, then the surface for `target`, and calls `onDone`
     * exactly once: before returning (wgpu-native) or later from the event
     * loop (web).
     */
    static void create(const Type::Target::Any& target, Type::PowerPreference powerPreference, Callback onDone);

    Engine(Engine&&) noexcept = default;
    // Assigning would destroy the old context before the old surface.
    Engine& operator=(Engine&&) = delete;

    [[nodiscard]] const Type::Info& info() const { return mContext->info; }

    /** Surface size in physical pixels. Zero sizes are ignored. */
    void resize(std::uint32_t width, std::uint32_t height);

    /** Sets the time frames are drawn at; passes read it as Frame::timeMs(). */
    void update(double nowMs) { mNowMs = nowMs; }

    /** Marks the next frame as needed, e.g. after the data or view changed. */
    void requestRender() { mDirty = true; }

    /** Whether something changed since the last drawn frame. */
    [[nodiscard]] bool needsRender() const { return mDirty; }

    /**
     * Turns timing on or off at runtime. Off by default; when off, a frame
     * reads no clocks. Has no effect if profiling is compiled out.
     */
    void setProfiling(bool enabled) { mProfiling = enabled; }
    [[nodiscard]] bool profiling() const { return mProfiling; }

    /** Stats of the last drawn frame. */
    [[nodiscard]] const Type::FrameStats& stats() const { return mStats; }

    /** Appends a pass; passes run in the order they were added. */
    void addPass(std::unique_ptr<Pass> pass);

    /**
     * Draws one frame, whether or not it is needed, unless the surface skips
     * it. A skipped frame stays needed.
     */
    void render();

private:
    Engine(std::unique_ptr<Context> context, std::unique_ptr<Surface> surface) noexcept;

    // Destroyed in reverse order: passes, surface, then the context they use.
    std::unique_ptr<Context> mContext;
    std::unique_ptr<Surface> mSurface;
    std::vector<std::unique_ptr<Pass>> mPasses;

    std::uint32_t mWidth = 0;
    std::uint32_t mHeight = 0;
    double mNowMs = 0.0;
    bool mDirty = true;
    bool mProfiling = false;
    Type::FrameStats mStats;
};

} // namespace Gpu
