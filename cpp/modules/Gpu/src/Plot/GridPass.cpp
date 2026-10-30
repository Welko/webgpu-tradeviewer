#include "Gpu/Plot/GridPass.hpp"

#include "Gpu/Convert.hpp"
#include "Gpu/Shaders/GridPass.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace Gpu::Plot {

namespace {

constexpr WGPUColor ClearColor{ 0.07, 0.08, 0.10, 1.0 };

/** One line as the shader reads it (GridPass.wgsl). */
struct GpuLine
{
    float position;
    std::uint32_t vertical;  // 1 or 0
};
static_assert(sizeof(GpuLine) == 8);

/** The shader's Plot uniform (GridPass.wgsl). */
struct Uniforms
{
    float width;
    float height;
    std::array<float, 2> padding;  // uniform structs are sized in 16-byte steps
};
static_assert(sizeof(Uniforms) == 16);

Handle<WGPURenderPipeline> createPipeline(WGPUDevice device, WGPUTextureFormat format)
{
    WGPUShaderSourceWGSL source{};
    source.chain.sType = WGPUSType_ShaderSourceWGSL;
    source.code = Convert::ToWgpu::stringView(Shaders::GridPass);
    WGPUShaderModuleDescriptor moduleDescriptor{};
    moduleDescriptor.nextInChain = &source.chain;
    const Handle module(wgpuDeviceCreateShaderModule(device, &moduleDescriptor));

    std::array<WGPUVertexAttribute, 2> attributes{};
    attributes[0].format = WGPUVertexFormat_Float32;
    attributes[0].offset = offsetof(GpuLine, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Uint32;
    attributes[1].offset = offsetof(GpuLine, vertical);
    attributes[1].shaderLocation = 1;

    WGPUVertexBufferLayout lines{};
    lines.stepMode = WGPUVertexStepMode_Instance;
    lines.arrayStride = sizeof(GpuLine);
    lines.attributeCount = attributes.size();
    lines.attributes = attributes.data();

    // Premultiplied alpha.
    WGPUBlendState blend{};
    blend.color = { .operation = WGPUBlendOperation_Add, .srcFactor = WGPUBlendFactor_One,
                    .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha };
    blend.alpha = blend.color;

    WGPUColorTargetState target{};
    target.format = format;
    target.blend = &blend;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment{};
    fragment.module = module.get();
    fragment.entryPoint = Convert::ToWgpu::stringView("fragmentMain");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor descriptor{};
    descriptor.vertex.module = module.get();
    descriptor.vertex.entryPoint = Convert::ToWgpu::stringView("vertexMain");
    descriptor.vertex.bufferCount = 1;
    descriptor.vertex.buffers = &lines;
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    descriptor.multisample.count = 1;
    descriptor.multisample.mask = ~0u;
    descriptor.fragment = &fragment;
    // No layout: WebGPU derives it from the shader.
    return Handle(wgpuDeviceCreateRenderPipeline(device, &descriptor));
}

} // namespace

GridPass::GridPass(const Context& context, Resource::Stats& stats, WGPUTextureFormat format)
    : mContext(&context),
      mPipeline(createPipeline(context.device.get(), format))
{
    WGPUBufferDescriptor uniformsDescriptor{};
    uniformsDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformsDescriptor.size = sizeof(Uniforms);
    mUniforms = Handle<WGPUBuffer>(wgpuDeviceCreateBuffer(context.device.get(), &uniformsDescriptor), stats, sizeof(Uniforms));

    WGPUBufferDescriptor linesDescriptor{};
    linesDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    linesDescriptor.size = MaxLines * sizeof(GpuLine);
    mLines = Handle<WGPUBuffer>(wgpuDeviceCreateBuffer(context.device.get(), &linesDescriptor), stats, linesDescriptor.size);

    const Handle layout(wgpuRenderPipelineGetBindGroupLayout(mPipeline.get(), 0));
    WGPUBindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = mUniforms.get();
    entry.size = sizeof(Uniforms);
    WGPUBindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = layout.get();
    bindGroupDescriptor.entryCount = 1;
    bindGroupDescriptor.entries = &entry;
    mBindGroup = Handle(wgpuDeviceCreateBindGroup(context.device.get(), &bindGroupDescriptor));
}

void GridPass::setGrid(const Type::Grid& grid)
{
    std::vector<GpuLine> lines;
    lines.reserve(grid.verticalLines.size() + grid.horizontalLines.size());
    for (const float position : grid.verticalLines) {
        lines.push_back({ .position = position, .vertical = 1 });
    }
    for (const float position : grid.horizontalLines) {
        lines.push_back({ .position = position, .vertical = 0 });
    }
    mLineCount = std::min<std::uint32_t>(static_cast<std::uint32_t>(lines.size()), MaxLines);
    if (mLineCount > 0) {
        wgpuQueueWriteBuffer(mContext->queue.get(), mLines.get(), 0, lines.data(), mLineCount * sizeof(GpuLine));
    }
}

void GridPass::encode(WGPUCommandEncoder encoder, WGPUTextureView target, Area plot, Profiler& profiler)
{
    WGPURenderPassColorAttachment color{};
    color.view = target;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = ClearColor;

    WGPURenderPassDescriptor descriptor{};
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    descriptor.timestampWrites = profiler.measurePass("grid");
    const Handle pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));

    if (mLineCount > 0 && !plot.empty()) {
        const Uniforms uniforms{ .width = static_cast<float>(plot.width), .height = static_cast<float>(plot.height), .padding = {} };
        wgpuQueueWriteBuffer(mContext->queue.get(), mUniforms.get(), 0, &uniforms, sizeof(uniforms));
        plot.apply(pass.get());
        wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, mBindGroup.get(), 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass.get(), 0, mLines.get(), 0, mLineCount * sizeof(GpuLine));
        wgpuRenderPassEncoderDraw(pass.get(), 6, mLineCount, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass.get());
}

} // namespace Gpu::Plot
