#pragma once

#include "Gpu/Pass.hpp"

#include <webgpu/webgpu.h>

namespace Gpu {

/** Fills the frame with one color. Usually the first pass. */
class Pass::Clear final : public Pass
{
public:
    explicit Clear(WGPUColor color) noexcept;

    [[nodiscard]] std::string_view name() const override { return "clear"; }
    void encode(Frame& frame) override;

private:
    WGPUColor mColor;
};

} // namespace Gpu
