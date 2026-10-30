#include "Gpu/Market/TradesPass.hpp"

#include "Gpu/Convert.hpp"
#include "Gpu/Market/Marker.hpp"
#include "Gpu/Shaders/TradesPass.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace Gpu::Market {

namespace {

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

/** The shader's Markers uniform (TradesPass.wgsl). */
struct GpuMarkers
{
    std::array<float, 3> buyColor;
    float opacity;
    std::array<float, 3> sellColor;
    std::uint32_t scale;  // 0 area, 1 log
    float minRadius;
    float maxRadius;
    float areaRadiusAt1Btc;
    float logLowest;
    float logHighest;
    std::array<float, 3> padding;  // cursorColor starts on 16 bytes
    std::array<float, 3> cursorColor;
    float padding2;  // uniform structs are sized in 16-byte steps
};
static_assert(sizeof(GpuMarkers) == 80);

/** The shader's View uniform (TradesPass.wgsl). */
struct Uniforms
{
    std::uint32_t startHigh;
    std::uint32_t startLow;
    float lengthUs;
    float priceLow;
    float priceSpan;
    float width;
    float height;
    float pixelRatio;
    GpuMarkers markers;
};
static_assert(sizeof(Uniforms) == 112);

std::array<float, 3> toGpu(Gpu::Type::Color color)
{
    return { color.red, color.green, color.blue };
}


/** The uniforms, for both pipelines; WebGPU's derived layouts would differ per pipeline. */
Handle<WGPUBindGroupLayout> createBindGroupLayout(WGPUDevice device)
{
    WGPUBindGroupLayoutEntry entry{};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.minBindingSize = sizeof(Uniforms);
    WGPUBindGroupLayoutDescriptor descriptor{};
    descriptor.entryCount = 1;
    descriptor.entries = &entry;
    return Handle(wgpuDeviceCreateBindGroupLayout(device, &descriptor));
}

Handle<WGPUShaderModule> createShaderModule(WGPUDevice device)
{
    WGPUShaderSourceWGSL source{};
    source.chain.sType = WGPUSType_ShaderSourceWGSL;
    source.code = Convert::ToWgpu::stringView(Shaders::TradesPass);
    WGPUShaderModuleDescriptor descriptor{};
    descriptor.nextInChain = &source.chain;
    return Handle(wgpuDeviceCreateShaderModule(device, &descriptor));
}

/** Draws each trade instance with the shader's `vertexEntry` and `fragmentEntry`. */
Handle<WGPURenderPipeline> createPipeline(WGPUDevice device, WGPUTextureFormat format, WGPUShaderModule module,
                                          WGPUBindGroupLayout bindGroupLayout, const char* vertexEntry, const char* fragmentEntry)
{
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
    fragment.module = module;
    fragment.entryPoint = Convert::ToWgpu::stringView(fragmentEntry);
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPUPipelineLayoutDescriptor layoutDescriptor{};
    layoutDescriptor.bindGroupLayoutCount = 1;
    layoutDescriptor.bindGroupLayouts = &bindGroupLayout;
    const Handle layout(wgpuDeviceCreatePipelineLayout(device, &layoutDescriptor));

    WGPURenderPipelineDescriptor descriptor{};
    descriptor.layout = layout.get();
    descriptor.vertex.module = module;
    descriptor.vertex.entryPoint = Convert::ToWgpu::stringView(vertexEntry);
    descriptor.vertex.bufferCount = 1;
    descriptor.vertex.buffers = &trades;
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    descriptor.multisample.count = 1;
    descriptor.multisample.mask = ~0u;
    descriptor.fragment = &fragment;
    return Handle(wgpuDeviceCreateRenderPipeline(device, &descriptor));
}

} // namespace

TradesPass::TradesPass(const Context& context, Resource::Stats& stats, WGPUTextureFormat format)
    : mContext(&context),
      mTrades(context, stats, WGPUBufferUsage_Vertex)
{
    const WGPUDevice device = context.device.get();
    const auto layout = createBindGroupLayout(device);
    const auto module = createShaderModule(device);
    mPipeline = createPipeline(device, format, module.get(), layout.get(), "vertexMain", "fragmentMain");
    mRingPipeline = createPipeline(device, format, module.get(), layout.get(), "ringVertex", "ringFragment");

    WGPUBufferDescriptor uniformsDescriptor{};
    uniformsDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformsDescriptor.size = sizeof(Uniforms);
    mUniforms = Handle<WGPUBuffer>(wgpuDeviceCreateBuffer(context.device.get(), &uniformsDescriptor), stats, sizeof(Uniforms));

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

void TradesPass::encode(WGPUCommandEncoder encoder, WGPUTextureView target, Plot::Area plot, Profiler& profiler)
{
    const auto startUs = static_cast<std::uint64_t>(mView.time.startUs);
    const Uniforms uniforms{
        .startHigh = static_cast<std::uint32_t>(startUs >> 32),
        .startLow = static_cast<std::uint32_t>(startUs),
        .lengthUs = static_cast<float>(mView.time.getLengthUs()),
        .priceLow = static_cast<float>(mView.price.low),
        .priceSpan = static_cast<float>(mView.price.high - mView.price.low),
        .width = static_cast<float>(plot.width),
        .height = static_cast<float>(plot.height),
        .pixelRatio = mPixelRatio,
        .markers = {
            .buyColor = toGpu(mTheme.buy),
            .opacity = mTheme.markerOpacity,
            .sellColor = toGpu(mTheme.sell),
            .scale = mMarkerScale == Type::MarkerScale::Area ? 0u : 1u,
            .minRadius = Marker::MinRadius,
            .maxRadius = Marker::MaxRadius,
            .areaRadiusAt1Btc = Marker::AreaRadiusAt1Btc,
            .logLowest = Marker::LogLowest,
            .logHighest = Marker::LogHighest,
            .padding = {},
            .cursorColor = toGpu(mTheme.cursor),
            .padding2 = 0.0f,
        },
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
    descriptor.timestampWrites = profiler.measurePass("trades");
    const Handle pass(wgpuCommandEncoderBeginRenderPass(encoder, &descriptor));

    // Only trades already uploaded can be drawn.
    const std::size_t uploaded = mTrades.size() / sizeof(GpuTrade);
    const std::size_t first = std::min(mView.trades.first, uploaded);
    const std::size_t count = std::min(mView.trades.last, uploaded) - first;
    if (count > 0 && !plot.empty() && mView.time.getLengthUs() > 0 && mView.price.high > mView.price.low) {
        plot.apply(pass.get());
        wgpuRenderPassEncoderSetPipeline(pass.get(), mPipeline.get());
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, mBindGroup.get(), 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass.get(), 0, mTrades.get(), 0, mTrades.size());
        wgpuRenderPassEncoderDraw(pass.get(), 6, static_cast<std::uint32_t>(count), 0, static_cast<std::uint32_t>(first));
        // On top of every trade.
        if (mHighlight && *mHighlight < uploaded) {
            wgpuRenderPassEncoderSetPipeline(pass.get(), mRingPipeline.get());
            wgpuRenderPassEncoderDraw(pass.get(), 6, 1, 0, static_cast<std::uint32_t>(*mHighlight));
        }
    }
    wgpuRenderPassEncoderEnd(pass.get());
}

} // namespace Gpu::Market
