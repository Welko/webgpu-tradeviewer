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

    /** Appends a pass; passes run in the order they were added. */
    void addPass(std::unique_ptr<Pass> pass);

    /** Draws one frame, unless the surface skips it. */
    void render();

private:
    Engine(std::unique_ptr<Context> context, std::unique_ptr<Surface> surface) noexcept;

    // Destroyed in reverse order: passes, surface, then the context they use.
    std::unique_ptr<Context> mContext;
    std::unique_ptr<Surface> mSurface;
    std::vector<std::unique_ptr<Pass>> mPasses;
};

} // namespace Gpu
