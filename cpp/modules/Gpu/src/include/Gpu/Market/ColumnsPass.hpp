#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/GrowableBuffer.hpp"
#include "Gpu/Handle.hpp"
#include "Gpu/Market/Type.hpp"
#include "Gpu/Plot/Area.hpp"
#include "Gpu/Profiler.hpp"
#include "Gpu/Resource.hpp"

#include <webgpu/webgpu.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Gpu::Market {

/**
 * Draws summaries of the trades instead of their markers, over the grid and
 * inside the plot: in each column, a bar from the lowest to the highest
 * price, colored from sell to buy by the buys' share of the amount, more
 * opaque the more was traded. Each column is one instance of a quad, 20 bytes
 * in a vertex buffer.
 */
class ColumnsPass
{
public:
    /** The Context and Stats must outlive the pass. */
    ColumnsPass(const Context& context, Resource::Stats& stats, WGPUTextureFormat format);

    void setColumns(const Type::Columns& columns);
    void setView(const Type::View& view) { mView = view; }
    void setTheme(const Type::Theme& theme) { mTheme = theme; }

    /** Physical pixels per CSS or window pixel, which Style::ColumnMinHeight is given in. */
    void setPixelRatio(float ratio) { mPixelRatio = ratio; }

    /** The column to draw in the cursor color, by its position among those set; none for nothing. */
    void setHighlight(std::optional<std::size_t> position) { mHighlight = position; }

    /** Records the pass into `target`, drawing inside `plot`. */
    void encode(WGPUCommandEncoder encoder, WGPUTextureView target, Plot::Area plot, Profiler& profiler);

private:
    const Context* mContext;
    Handle<WGPURenderPipeline> mPipeline;
    Handle<WGPUBuffer> mUniforms;
    Handle<WGPUBindGroup> mBindGroup;
    GrowableBuffer mColumns;

    std::uint32_t mAcross = 0;  // columns splitting the plot
    Type::View mView{};
    Type::Theme mTheme;
    float mPixelRatio = 1.0f;
    std::optional<std::size_t> mHighlight;
};

} // namespace Gpu::Market
