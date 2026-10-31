#pragma once

#include "Gpu/Plot/Type.hpp"

#include <webgpu/webgpu.h>

#include <cstdint>

namespace Gpu::Plot {

/** The part of the frame that shows trades, inside the axis margins, in physical pixels from the top left. */
struct Area
{
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;

    /** A `width` × `height` frame less `insets`; empty if they leave no room. */
    static Area within(std::uint32_t width, std::uint32_t height, Type::Insets insets)
    {
        if (insets.left + insets.right >= width || insets.top + insets.bottom >= height) {
            return {};
        }
        return {
            .x = insets.left,
            .y = insets.top,
            .width = width - insets.left - insets.right,
            .height = height - insets.top - insets.bottom,
        };
    }

    [[nodiscard]] bool empty() const { return width == 0 || height == 0; }

    /** Makes a pass draw only here: clip space maps onto this area. */
    void apply(WGPURenderPassEncoder pass) const
    {
        wgpuRenderPassEncoderSetViewport(pass, static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
                                         static_cast<float>(height), 0.0f, 1.0f);
    }
};

} // namespace Gpu::Plot
