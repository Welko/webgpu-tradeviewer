#pragma once

#include "Gpu/Context.hpp"

#include <webgpu/webgpu.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

/**
 * Copies GPU data back to the CPU. Results arrive later.
 */
namespace Gpu::Readback {

using Bytes = std::vector<std::uint8_t>;
using Callback = std::function<void(std::expected<Bytes, std::string>)>;

/**
 * Copies the first `size` bytes of a MapRead `buffer` out, then calls
 * `onDone` exactly once. Holds its own reference to the buffer until then.
 */
void map(WGPUBuffer buffer, std::uint64_t size, Callback onDone);

/**
 * Copies a CopySrc RGBA8 texture back as tightly packed rows, top row first.
 * Calls `onDone` exactly once. Its staging buffer isn't tracked.
 */
void texture(const Context& context, WGPUTexture texture, std::uint32_t width, std::uint32_t height, Callback onDone);

} // namespace Gpu::Readback
