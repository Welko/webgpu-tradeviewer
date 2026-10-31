#include "Gpu/Market/ColumnsPass.hpp"

#include "Gpu/Convert.hpp"
#include "Gpu/Market/Style.hpp"
#include "Gpu/Shaders/ColumnsPass.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace Gpu::Market {

namespace {

/** One column as the shader reads it (ColumnsPass.wgsl). */
struct GpuColumn
{
    std::uint32_t index;
    float priceLow;
    float priceHigh;
    float buyShare;  // of the column's amount
    float weight;  // the column's amount relative to the largest's, square-rooted so small ones still show
};
static_assert(sizeof(GpuColumn) == 20);

/** The shader's View uniform (ColumnsPass.wgsl). */
struct Uniforms
{
    float across;
    float priceLow;
    float priceSpan;
    float plotWidthPx;
    float plotHeightPx;
    float pixelRatio;
    float minOpacity;
    float minHeightCssPx;
    std::uint32_t highlight;
    std::array<float, 3> padding0;  // vec3f fields start on 16 bytes
    std::array<float, 3> buyColor;
    float padding1;
    std::array<float, 3> sellColor;
    float padding2;
    std::array<float, 3> cursorColor;
    float padding3;
};
static_assert(sizeof(Uniforms) == 96);

std::array<float, 3> toGpu(Gpu::Type::Color color)
{
    return { color.red, color.green, color.blue };
}

Handle<WGPURenderPipeline> createPipeline(WGPUDevice device, WGPUTextureFormat format)
{
    WGPUShaderSourceWGSL source{};
    source.chain.sType = WGPUSType_ShaderSourceWGSL;
    source.code = Convert::ToWgpu::stringView(Shaders::ColumnsPass);
    WGPUShaderModuleDescriptor moduleDescriptor{};
    moduleDescriptor.nextInChain = &source.chain;
    const Handle module(wgpuDeviceCreateShaderModule(device, &moduleDescriptor));

    std::array<WGPUVertexAttribute, 3> attributes{};
    attributes[0].format = WGPUVertexFormat_Uint32;
    attributes[0].offset = offsetof(GpuColumn, index);
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x2;
    attributes[1].offset = offsetof(GpuColumn, priceLow);
    attributes[1].shaderLocation = 1;
    attributes[2].format = WGPUVertexFormat_Float32x2;
    attributes[2].offset = offsetof(GpuColumn, buyShare);
    attributes[2].shaderLocation = 2;

    WGPUVertexBufferLayout columns{};
    columns.stepMode = WGPUVertexStepMode_Instance;
    columns.arrayStride = sizeof(GpuColumn);
    columns.attributeCount = attributes.size();
    columns.attributes = attributes.data();

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
    descriptor.vertex.buffers = &columns;
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    descriptor.multisample.count = 1;
    descriptor.multisample.mask = ~0u;
    descriptor.fragment = &fragment;
    // No layout: WebGPU derives it from the shader.
    return Handle(wgpuDeviceCreateRenderPipeline(device, &descriptor));
}

} // namespace

ColumnsPass::ColumnsPass(const Context& context, Resource::Stats& stats, WGPUTextureFormat format)
    : mContext(&context),
      mPipeline(createPipeline(context.device.get(), format)),
      mColumns(context, stats, WGPUBufferUsage_Vertex)
{
    WGPUBufferDescriptor uniformsDescriptor{};
    uniformsDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformsDescriptor.size = sizeof(Uniforms);
    mUniforms = Handle<WGPUBuffer>(wgpuDeviceCreateBuffer(context.device.get(), &uniformsDescriptor), stats, sizeof(Uniforms));

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

void ColumnsPass::setColumns(const Type::Columns& columns)
{
    mAcross = columns.across;
    double largest = 0.0;
    for (const auto& column : columns.columns) {
        largest = std::max(largest, column.buyAmount + column.sellAmount);
    }
    std::vector<GpuColumn> packed;
    packed.reserve(columns.columns.size());
    for (const auto& column : columns.columns) {
        const double amount = column.buyAmount + column.sellAmount;
        packed.push_back({
            .index = column.index,
            .priceLow = static_cast<float>(column.price.low),
            .priceHigh = static_cast<float>(column.price.high),
            .buyShare = amount > 0.0 ? static_cast<float>(column.buyAmount / amount) : 0.5f,
            .weight = largest > 0.0 ? static_cast<float>(std::sqrt(amount / largest)) : 1.0f,
        });
    }
    mColumns.clear();
    if (!packed.empty()) {
        mColumns.append(std::as_bytes(std::span(packed)));
    }
}

void ColumnsPass::encode(WGPUCommandEncoder encoder, WGPUTextureView target, Plot::Area plot, Profiler& profiler)
{
    const Uniforms uniforms{
        .across = static_cast<float>(mAcross),
        .priceLow = static_cast<float>(mView.price.low),
        .priceSpan = static_cast<float>(mView.price.high - mView.price.low),
        .plotWidthPx = static_cast<float>(plot.width),
        .plotHeightPx = static_cast<float>(plot.height),
        .pixelRatio = mPixelRatio,
        .minOpacity = Style::ColumnMinOpacity,
        .minHeightCssPx = Style::ColumnMinHeightCssPx,
        .highlight = mHighlight ? static_cast<std::uint32_t>(*mHighlight) : std::numeric_limits<std::uint32_t>::max(),
        .padding0 = {},
        .buyColor = toGpu(mTheme.buy),
        .padding1 = 0.0f,
        .sellColor = toGpu(mTheme.sell),
        .padding2 = 0.0f,
        .cursorColor = toGpu(mTheme.cursor),
        .padding3 = 0.0f,
    };
    wgpuQueueWriteBuffer(mContext->queue.get(), mUniforms.get(), 0, &uniforms, sizeof(uniforms));

    WGPURenderPassColorAttachment color{};
    color.view = target;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    // Over the grid.
    color.loadOp = WGPULoadOp_Load;
    color.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor descriptor{};
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    descriptor.timestampWrites = profiler.measurePass("columns");
    const Handle pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));

    const auto count = static_cast<std::uint32_t>(mColumns.size() / sizeof(GpuColumn));
    if (count > 0 && mAcross > 0 && !plot.empty() && mView.price.high > mView.price.low) {
        plot.apply(pass.get());
        wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, mBindGroup.get(), 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass.get(), 0, mColumns.get(), 0, mColumns.size());
        wgpuRenderPassEncoderDraw(pass.get(), 6, count, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass.get());
}

} // namespace Gpu::Market
