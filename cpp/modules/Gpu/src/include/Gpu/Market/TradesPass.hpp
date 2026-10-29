#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/GrowableBuffer.hpp"
#include "Gpu/Handle.hpp"
#include "Gpu/Market/Type.hpp"
#include "Gpu/Profiler.hpp"
#include "Gpu/Resource.hpp"

#include "Core/Data/Trades.hpp"

#include <webgpu/webgpu.h>

#include <cstddef>
#include <cstdint>

namespace Gpu::Market {

/**
 * Clears the frame and draws each trade in the view as a disc: time across,
 * price up, colored by side, sized by amount.
 *
 * Each trade is one instance of a quad, 20 bytes in a vertex buffer:
 * time in microseconds as two u32 halves, price and amount as f32, and side.
 * The shader subtracts the view's start in 64-bit integer math before
 * converting to f32, so positions stay exact at any zoom.
 */
class TradesPass
{
public:
    /** The Context and Stats must outlive the pass. */
    TradesPass(const Context& context, Resource::Stats& stats, WGPUTextureFormat format);

    /** Makes room for `count` trades in total, so appending up to there doesn't grow the buffer. */
    void reserve(std::size_t count);

    /** Removes every trade, keeping the room. */
    void clear();

    /** Adds trades after the current ones; they must not be earlier. */
    void append(const Core::Data::Trades& trades);

    void setView(const Type::View& view) { mView = view; }
    void setMarkerScale(Type::MarkerScale scale) { mMarkerScale = scale; }

    /** Physical pixels per CSS or window pixel, which marker sizes are given in. */
    void setPixelRatio(float ratio) { mPixelRatio = ratio; }

    /** Records the pass into `target`, `width` × `height` pixels. */
    void encode(WGPUCommandEncoder encoder, WGPUTextureView target, std::uint32_t width, std::uint32_t height,
                Profiler& profiler);

private:
    const Context* mContext;
    Handle<WGPURenderPipeline> mPipeline;
    Handle<WGPUBuffer> mUniforms;
    Handle<WGPUBindGroup> mBindGroup;
    GrowableBuffer mTrades;

    Type::View mView{};
    Type::MarkerScale mMarkerScale = Type::MarkerScale::Area;
    float mPixelRatio = 1.0f;
};

} // namespace Gpu::Market
