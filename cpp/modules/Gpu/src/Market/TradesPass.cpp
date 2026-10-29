#include "Gpu/Market/TradesPass.hpp"

#include "Gpu/Convert.hpp"
#include "Gpu/Shaders/TradesPass.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace Gpu::Market {

namespace {

constexpr WGPUColor ClearColor{ 0.07, 0.08, 0.10, 1.0 };

/** One trade as the shader reads it (TradesPass.wgsl). */
struct GpuTrade
{
    std::uint32_t timeHigh;  // microseconds since the epoch, upper 32 bits
    std::uint32_t timeLow;
    float price;
    float amount;
    std::uint32_t side;  // 0 buy, 1 sell
};
static_assert(sizeof(GpuTrade) == 20);

/** The shader's View uniform (TradesPass.wgsl). */
struct Uniforms
{
    std::uint32_t startHigh;
    std::uint32_t startLow;
    float lengthUs;
    std::uint32_t markerScale;
    float priceLow;
    float priceSpan;
    float width;
    float height;
    float pixelRatio;
    std::array<float, 3> padding;  // uniform structs are sized in 16-byte steps
};
static_assert(sizeof(Uniforms) == 48);


Handle<WGPURenderPipeline> createPipeline(WGPUDevice device, WGPUTextureFormat format)
{
    WGPUShaderSourceWGSL source{};
    source.chain.sType = WGPUSType_ShaderSourceWGSL;
    source.code = Convert::ToWgpu::stringView(Shaders::TradesPass);
    WGPUShaderModuleDescriptor moduleDescriptor{};
    moduleDescriptor.nextInChain = &source.chain;
    const Handle module(wgpuDeviceCreateShaderModule(device, &moduleDescriptor));

    std::array<WGPUVertexAttribute, 4> attributes{};
    attributes[0].format = WGPUVertexFormat_Uint32x2;
    attributes[0].offset = offsetof(GpuTrade, timeHigh);
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32;
    attributes[1].offset = offsetof(GpuTrade, price);
    attributes[1].shaderLocation = 1;
    attributes[2].format = WGPUVertexFormat_Float32;
    attributes[2].offset = offsetof(GpuTrade, amount);
    attributes[2].shaderLocation = 2;
    attributes[3].format = WGPUVertexFormat_Uint32;
    attributes[3].offset = offsetof(GpuTrade, side);
    attributes[3].shaderLocation = 3;

    WGPUVertexBufferLayout trades{};
    trades.stepMode = WGPUVertexStepMode_Instance;
    trades.arrayStride = sizeof(GpuTrade);
    trades.attributeCount = attributes.size();
    trades.attributes = attributes.data();

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
    descriptor.vertex.buffers = &trades;
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    descriptor.multisample.count = 1;
    descriptor.multisample.mask = ~0u;
    descriptor.fragment = &fragment;
    // No layout: WebGPU derives it from the shader.
    return Handle(wgpuDeviceCreateRenderPipeline(device, &descriptor));
}

} // namespace

TradesPass::TradesPass(const Context& context, Resource::Stats& stats, WGPUTextureFormat format)
    : mContext(&context),
      mPipeline(createPipeline(context.device.get(), format)),
      mTrades(context, stats, WGPUBufferUsage_Vertex)
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

void TradesPass::reserve(std::size_t count)
{
    mTrades.reserve(count * sizeof(GpuTrade));
}

void TradesPass::clear()
{
    mTrades.clear();
}

void TradesPass::append(const Core::Data::Trades& trades)
{
    std::vector<GpuTrade> packed(trades.size());
    for (std::size_t i = 0; i < trades.size(); ++i) {
        const auto timeUs = static_cast<std::uint64_t>(trades.timeUs[i]);
        packed[i] = {
            .timeHigh = static_cast<std::uint32_t>(timeUs >> 32),
            .timeLow = static_cast<std::uint32_t>(timeUs),
            .price = static_cast<float>(trades.price[i]),
            .amount = static_cast<float>(trades.amount[i]),
            .side = trades.side[i] == Core::Data::Side::Buy ? 0u : 1u,
        };
    }
    mTrades.append(std::as_bytes(std::span(packed)));
}

void TradesPass::encode(WGPUCommandEncoder encoder, WGPUTextureView target, std::uint32_t width, std::uint32_t height,
                        Profiler& profiler)
{
    const auto startUs = static_cast<std::uint64_t>(mView.time.startUs);
    const Uniforms uniforms{
        .startHigh = static_cast<std::uint32_t>(startUs >> 32),
        .startLow = static_cast<std::uint32_t>(startUs),
        .lengthUs = static_cast<float>(mView.time.lengthUs()),
        .markerScale = mMarkerScale == Type::MarkerScale::Area ? 0u : 1u,
        .priceLow = static_cast<float>(mView.price.low),
        .priceSpan = static_cast<float>(mView.price.high - mView.price.low),
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .pixelRatio = mPixelRatio,
        .padding = {},
    };
    wgpuQueueWriteBuffer(mContext->queue.get(), mUniforms.get(), 0, &uniforms, sizeof(uniforms));

    WGPURenderPassColorAttachment color{};
    color.view = target;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = ClearColor;

    WGPURenderPassDescriptor descriptor{};
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    descriptor.timestampWrites = profiler.measurePass("trades");
    const Handle pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));

    // Only trades already uploaded can be drawn.
    const std::size_t uploaded = mTrades.size() / sizeof(GpuTrade);
    const std::size_t first = std::min(mView.trades.first, uploaded);
    const std::size_t count = std::min(mView.trades.last, uploaded) - first;
    if (count > 0 && mView.time.lengthUs() > 0 && mView.price.high > mView.price.low) {
        wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, mBindGroup.get(), 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass.get(), 0, mTrades.get(), 0, mTrades.size());
        wgpuRenderPassEncoderDraw(pass.get(), 6, static_cast<std::uint32_t>(count), 0, static_cast<std::uint32_t>(first));
    }
    wgpuRenderPassEncoderEnd(pass.get());
}

} // namespace Gpu::Market
