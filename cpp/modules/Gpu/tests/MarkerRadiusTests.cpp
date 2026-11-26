#include "DeviceTesting.hpp"

#include "Gpu/Context.hpp"
#include "Gpu/Convert.hpp"
#include "Gpu/Handle.hpp"
#include "Gpu/Market/TradesPass.hpp"
#include "Gpu/Readback.hpp"
#include "Gpu/Shaders/TradesPass.hpp"

#include "Core/Plot/Sizes.hpp"

#include <doctest/doctest.h>

#include <webgpu/webgpu.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Core::Plot::MarkerScale;
using Core::Plot::MarkerSizing;
using Gpu::Handle;
using Gpu::Market::TradesPass;

// Runs the shader's own markerRadiusPx() on each amount. Appended to the
// shipped shader here, so it carries no test code.
constexpr std::string_view RadiusCompute = R"(
@group(0) @binding(1) var<storage, read> amounts: array<f32>;
@group(0) @binding(2) var<storage, read_write> radii: array<f32>;

@compute @workgroup_size(64)
fn computeRadii(@builtin(global_invocation_id) id: vec3u) {
    if (id.x < arrayLength(&amounts)) {
        radii[id.x] = markerRadiusPx(amounts[id.x]);
    }
}
)";

/** Amounts from 1e-9 to 1e5, evenly spread in their logarithm: past both ends of every sizing tested. */
std::vector<float> makeAmounts()
{
    constexpr std::size_t Count = 4096;
    std::vector<float> amounts(Count);
    for (std::size_t i = 0; i < Count; ++i)
    {
        amounts[i] = static_cast<float>(std::pow(10.0, -9.0 + (14.0 * static_cast<double>(i) / (Count - 1))));
    }
    return amounts;
}

Handle<WGPUBuffer> createBuffer(const Gpu::Context& context, std::uint64_t size, WGPUBufferUsage usage)
{
    WGPUBufferDescriptor descriptor{};
    descriptor.size = size;
    descriptor.usage = usage;
    return Handle(wgpuDeviceCreateBuffer(context.device.get(), &descriptor));
}

/** The shader's radius of each of `amounts`, in physical pixels at a pixel ratio of 1, or nothing, with a failed check. */
std::optional<std::vector<float>> computeRadii(const Gpu::Context& context, const MarkerSizing& sizing, const std::vector<float>& amounts)
{
    const std::string source = std::string(Gpu::Shaders::TradesPass) + std::string(RadiusCompute);
    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = Gpu::Convert::ToWgpu::stringView(source);
    WGPUShaderModuleDescriptor moduleDescriptor{};
    moduleDescriptor.nextInChain = &wgsl.chain;
    const Handle module(wgpuDeviceCreateShaderModule(context.device.get(), &moduleDescriptor));

    WGPUComputePipelineDescriptor pipelineDescriptor{};
    pipelineDescriptor.compute.module = module.get();
    pipelineDescriptor.compute.entryPoint = Gpu::Convert::ToWgpu::stringView("computeRadii");
    const Handle pipeline(wgpuDeviceCreateComputePipeline(context.device.get(), &pipelineDescriptor));

    // Only the markers and the pixel ratio matter to markerRadiusPx().
    TradesPass::Uniforms uniforms{};
    uniforms.pixelRatio = 1.0f;
    uniforms.markers = TradesPass::toGpuMarkers({}, sizing);
    const std::uint64_t bytes = amounts.size() * sizeof(float);
    const auto uniformBuffer = createBuffer(context, sizeof(uniforms), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    const auto amountBuffer = createBuffer(context, bytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
    const auto radiusBuffer = createBuffer(context, bytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    const auto readBuffer = createBuffer(context, bytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(context.queue.get(), uniformBuffer.get(), 0, &uniforms, sizeof(uniforms));
    wgpuQueueWriteBuffer(context.queue.get(), amountBuffer.get(), 0, amounts.data(), bytes);

    const auto entry = [](std::uint32_t binding, WGPUBuffer buffer, std::uint64_t size) {
        WGPUBindGroupEntry bound{};
        bound.binding = binding;
        bound.buffer = buffer;
        bound.size = size;
        return bound;
    };
    const std::array entries{
        entry(0, uniformBuffer.get(), sizeof(uniforms)),
        entry(1, amountBuffer.get(), bytes),
        entry(2, radiusBuffer.get(), bytes),
    };
    const Handle layout(wgpuComputePipelineGetBindGroupLayout(pipeline.get(), 0));
    WGPUBindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = layout.get();
    bindGroupDescriptor.entryCount = entries.size();
    bindGroupDescriptor.entries = entries.data();
    const Handle bindGroup(wgpuDeviceCreateBindGroup(context.device.get(), &bindGroupDescriptor));

    const Handle encoder(wgpuDeviceCreateCommandEncoder(context.device.get(), nullptr));
    {
        const Handle pass(wgpuCommandEncoderBeginComputePass(encoder.get(), nullptr));
        wgpuComputePassEncoderSetPipeline(pass.get(), pipeline.get());
        wgpuComputePassEncoderSetBindGroup(pass.get(), 0, bindGroup.get(), 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass.get(), static_cast<std::uint32_t>((amounts.size() + 63) / 64), 1, 1);
        wgpuComputePassEncoderEnd(pass.get());
    }
    wgpuCommandEncoderCopyBufferToBuffer(encoder.get(), radiusBuffer.get(), 0, readBuffer.get(), 0, bytes);
    const Handle commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
    context.submit(commands.get());

    std::optional<std::vector<float>> radii;
    bool done = false;
    Gpu::Readback::map(readBuffer.get(), bytes, [&](std::expected<Gpu::Readback::Bytes, std::string> result) {
        done = true;
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result)
        {
            radii.emplace(amounts.size());
            std::memcpy(radii->data(), result->data(), bytes);
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline)
    {
        wgpuInstanceProcessEvents(context.instance.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(done, "The radii never came back");
    return radii;
}

}  // namespace

TEST_CASE("The shader sizes markers as MarkerSizing does, on both scales")
{
    const auto context = DeviceTesting::createContext();
    if (!context)
    {
        return;
    }
    const auto amounts = makeAmounts();
    for (const auto scale : { MarkerScale::Area, MarkerScale::Log })
    {
        for (const auto& [low, high] : { std::pair{ 0.01, 1.0 }, std::pair{ 2e-5, 5.9 }, std::pair{ 1e-6, 1e-3 }, std::pair{ 1.0, 1e4 } })
        {
            const MarkerSizing sizing{ .scale = scale, .lowAmount = low, .highAmount = high };
            CAPTURE(scale);
            CAPTURE(low);
            CAPTURE(high);
            const auto radii = computeRadii(*context, sizing, amounts);
            if (!radii)
            {
                return;
            }
            // In f32 on the GPU, and double on the CPU from the same f32 amounts.
            double worst = 0.0;
            for (std::size_t i = 0; i < amounts.size(); ++i)
            {
                worst = std::max(worst, std::abs((*radii)[i] - static_cast<double>(sizing.getRadiusCssPx(amounts[i]))));
            }
            CHECK(worst <= 0.001);
        }
    }
}
