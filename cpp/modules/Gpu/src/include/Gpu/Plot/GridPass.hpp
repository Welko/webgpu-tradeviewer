#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/Handle.hpp"
#include "Gpu/Plot/Area.hpp"
#include "Gpu/Plot/Type.hpp"
#include "Gpu/Profiler.hpp"
#include "Gpu/Resource.hpp"

#include <webgpu/webgpu.h>

#include <cstdint>

namespace Gpu::Plot {

/**
 * Clears the frame to the theme's background and draws the grid: lines
 * across the plot, usually at the axis ticks, under whatever is drawn next.
 * Each line is one instance of a quad, 8 bytes in a vertex buffer with room
 * for MaxLines.
 */
class GridPass
{
public:
    /** More than any axis shows; extra lines are left out. */
    static constexpr std::uint32_t MaxLines = 128;

    /** The Context and Stats must outlive the pass. */
    GridPass(const Context& context, Resource::Stats& stats, WGPUTextureFormat format);

    void setGrid(const Type::Grid& grid);
    void setTheme(const Type::Theme& theme) { mTheme = theme; }

    /** Records the pass into `target`, drawing inside `plot`. */
    void encode(WGPUCommandEncoder encoder, WGPUTextureView target, Area plot, Profiler& profiler);

private:
    const Context* mContext;
    Handle<WGPURenderPipeline> mPipeline;
    Handle<WGPUBuffer> mUniforms;
    Handle<WGPUBuffer> mLines;
    Handle<WGPUBindGroup> mBindGroup;
    std::uint32_t mLineCount = 0;
    Type::Theme mTheme;
};

} // namespace Gpu::Plot
