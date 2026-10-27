#pragma once

#include "Gpu/Context.hpp"

#include <webgpu/webgpu.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

/**
 * Copying GPU data back to the CPU: pixels for image tests or screenshots,
 * later buffers with compute results.
 */
namespace Gpu::Readback {

using Bytes = std::vector<std::uint8_t>;
using Callback = std::function<void(std::expected<Bytes, std::string>)>;

/**
 * Copies an RGBA8 texture, created with CopySrc usage, back to the CPU as
 * tightly packed rows, top row first. Calls `onDone` exactly once, later:
 * natively when WebGPU events are processed, on the web from the browser's
 * event loop. The short-lived staging buffer isn't counted in
 * Resource::Stats.
 */
void texture(const Context& context, WGPUTexture texture, std::uint32_t width, std::uint32_t height, Callback onDone);

} // namespace Gpu::Readback
