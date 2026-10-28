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
 * GPU timings, later compute results. Results arrive later: natively when
 * WebGPU events are processed, on the web from the browser's event loop.
 */
namespace Gpu::Readback {

using Bytes = std::vector<std::uint8_t>;
using Callback = std::function<void(std::expected<Bytes, std::string>)>;

/**
 * Maps `buffer`, created with MapRead usage, copies its first `size` bytes
 * out and unmaps it again, then calls `onDone` exactly once. Keeps its own
 * reference to the buffer until then, so the caller may release it.
 */
void map(WGPUBuffer buffer, std::uint64_t size, Callback onDone);

/**
 * Copies an RGBA8 texture, created with CopySrc usage, back to the CPU as
 * tightly packed rows, top row first. Calls `onDone` exactly once. The
 * short-lived staging buffer isn't counted in Resource::Stats.
 */
void texture(const Context& context, WGPUTexture texture, std::uint32_t width, std::uint32_t height, Callback onDone);

} // namespace Gpu::Readback
