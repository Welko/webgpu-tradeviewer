#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/GrowableBuffer.hpp"
#include "Gpu/Handle.hpp"
#include "Gpu/Market/Type.hpp"
#include "Gpu/Plot/Area.hpp"
#include "Gpu/Profiler.hpp"
#include "Gpu/Resource.hpp"

#include "Core/Data/Trades.hpp"
#include "Core/Plot/Sizes.hpp"

#include <webgpu/webgpu.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace Gpu::Market {

/**
 * Draws each trade in the view as a disc, over the grid and inside the plot:
 * time across, price up, colored by side, sized by amount. A ring marks the
 * highlighted trade, on top of the rest.
 *
 * Each trade is one instance of a triangle around its disc, 20 bytes in a
 * vertex buffer: time in microseconds as two u32 halves, price and amount as
 * f32, and side. The shader subtracts the view's start in 64-bit integer math
 * before converting to f32, so positions stay exact at any zoom.
 */
class TradesPass
{
public:
    /** The shader's Markers uniform (TradesPass.wgsl). */
    struct GpuMarkers
    {
        std::array<float, 3> buyColor;
        float opacity;
        std::array<float, 3> sellColor;
        std::uint32_t scale;  // 0 area, 1 log
        float minRadiusCssPx;
        float maxRadiusCssPx;
        float lowAmount;
        float highAmount;
        std::array<float, 3> cursorColor;
        float padding;  // uniform structs are sized in 16-byte steps
    };

    /** The shader's View uniform (TradesPass.wgsl). */
    struct Uniforms
    {
        std::uint32_t startHigh;
        std::uint32_t startLow;
        float lengthUs;
        float priceLow;
        float priceSpan;
        float plotWidthPx;
        float plotHeightPx;
        float pixelRatio;
        GpuMarkers markers;
    };

    /** How `theme` and `sizing` reach the shader, the radii's bounds from Core/Plot/Sizes.hpp. */
    static GpuMarkers toGpuMarkers(const Type::Theme& theme, const Core::Plot::MarkerSizing& sizing);

    /** The Context and Stats must outlive the pass. */
    TradesPass(const Context& context, Resource::Stats& stats, WGPUTextureFormat format);

    /** Makes room for `count` trades in total, so appending up to there doesn't grow the buffer. Fails past getMaxCount(). */
    [[nodiscard]] std::expected<void, std::string> reserve(std::size_t count);

    /** The most trades the GPU's largest buffer holds. */
    [[nodiscard]] std::size_t getMaxCount() const;

    /** How many trades were appended since the last clear(). */
    [[nodiscard]] std::size_t getCount() const;

    /** Removes every trade, keeping the room. */
    void clear();

    /** Adds trades after the current ones; they must not be earlier. Fails, adding none, past getMaxCount(). */
    [[nodiscard]] std::expected<void, std::string> append(const Core::Data::Trades& trades);

    void setView(const Type::View& view) { mView = view; }
    void setMarkerSizing(const Core::Plot::MarkerSizing& sizing) { mMarkerSizing = sizing; }
    void setTheme(const Type::Theme& theme) { mTheme = theme; }

    /** The trade to ring, by its position among those appended; none for nothing. */
    void setHighlight(std::optional<std::size_t> index) { mHighlight = index; }

    /** Physical pixels per CSS or window pixel, which marker sizes are given in. */
    void setPixelRatio(float ratio) { mPixelRatio = ratio; }

    /** Records the pass into `target`, drawing inside `plot`. */
    void encode(WGPUCommandEncoder encoder, WGPUTextureView target, Plot::Area plot, Profiler& profiler);

private:
    const Context* mContext;
    Handle<WGPURenderPipeline> mPipeline;
    Handle<WGPURenderPipeline> mRingPipeline;
    Handle<WGPUBuffer> mUniforms;
    Handle<WGPUBindGroup> mBindGroup;
    GrowableBuffer mTrades;

    Type::View mView{};
    Core::Plot::MarkerSizing mMarkerSizing;
    Type::Theme mTheme;
    std::optional<std::size_t> mHighlight;
    float mPixelRatio = 1.0f;
};

// The sizes WGSL's layout rules give Markers and View in TradesPass.wgsl, which these must match field for field.
static_assert(sizeof(TradesPass::GpuMarkers) == 64);
static_assert(sizeof(TradesPass::Uniforms) == 96);

}  // namespace Gpu::Market
