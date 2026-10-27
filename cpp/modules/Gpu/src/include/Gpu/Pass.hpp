#pragma once

#include <string_view>

namespace Gpu {

class Frame;

/**
 * One step of drawing a frame, e.g. clearing or drawing the trades. The
 * engine runs its passes in order, each recording into the same Frame.
 * A pass owns the GPU objects it needs (pipelines, buffers).
 *
 * The pass is also the unit of GPU timing: WebGPU only allows timestamps
 * at pass boundaries, so something timed on its own needs its own pass.
 */
class Pass
{
public:
    class Clear;

    virtual ~Pass() = default;
    Pass(const Pass&) = delete;
    Pass& operator=(const Pass&) = delete;

    /** Identifies the pass in profiling stats. */
    [[nodiscard]] virtual std::string_view name() const = 0;

    /** Records this pass's commands into `frame`. */
    virtual void encode(Frame& frame) = 0;

protected:
    Pass() = default;
};

} // namespace Gpu
