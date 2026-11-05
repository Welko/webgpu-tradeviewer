#include "Gpu/Market/Engine.hpp"

#include "Gpu/Context.hpp"
#include "Gpu/Handle.hpp"
#include "Gpu/Market/ColumnsPass.hpp"
#include "Gpu/Market/TradesPass.hpp"
#include "Gpu/Plot/Area.hpp"
#include "Gpu/RenderTarget.hpp"
#include "Gpu/Plot/GridPass.hpp"
#include "Gpu/Profiler.hpp"
#include "Gpu/Resource.hpp"

#include "Core/Profiling.hpp"

#include <webgpu/webgpu.h>

#include <algorithm>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Gpu::Market {

namespace {

// Frames whose GPU times may be on their way at once.
constexpr std::size_t MaxWaitingFrames = 16;

/** Why setTrades() or appendTrades() refuses `count` trades. */
std::unexpected<std::string> tooManyTrades(std::size_t count, std::size_t max)
{
    return std::unexpected(std::format("{} trades are too many: at most {} fit on the GPU", count, max));
}

} // namespace

/** Everything behind Engine. */
struct Engine::Impl
{
    ~Impl();

    /** Applies the parts of `next` that differ from `settings`, or all of it. */
    void setSettings(const Settings& next, bool everything);

    void render();

    /** Frame timing: the profiler, and the frames waiting for its results. */
    struct Profiling
    {
        Profiler::StatsQueue statsQueue{ MaxWaitingFrames };
        std::shared_ptr<Profiler> profiler;  // set once the device exists
        bool enabled = false;
        std::uint64_t lastFrame = 0;  // frames drawn so far
    };

    /** In the order they draw. */
    struct Passes
    {
        Plot::GridPass grid;
        TradesPass trades;
        ColumnsPass columns;  // instead of trades, while showColumns
    };

    // Declared first so it is destroyed last, after every tracked handle.
    Resource::Stats resources;

    // Destroyed in reverse order: the passes, the render target, the profiler, then the context they use.
    Context context;
    Profiling profiling;
    std::optional<RenderTarget> renderTarget;  // set once the device exists
    std::optional<Passes> passes;  // set with the render target, whose format they draw in

    Settings settings;  // as last set
    std::optional<std::size_t> maxTrades;  // Params::maxTrades
    bool showColumns = false;  // summaries instead of trades
    double nowMs = 0.0;  // the time frames show, for time-based effects
    bool dirty = true;
};

// Creation

void Engine::create(const Params& params, CreatedCallback onDone)
{
    auto impl = std::make_unique<Impl>();
    impl->maxTrades = params.maxTrades;
    Handle instance(wgpuCreateInstance(nullptr));
    if (!instance) {
        onDone(std::unexpected("Failed to create a WebGPU instance"));
        return;
    }
    auto prepared = RenderTarget::prepare(instance.get(), params.target);
    if (!prepared) {
        onDone(std::unexpected(std::move(prepared.error())));
        return;
    }

    // Shared, because the callback must be copyable; freed even if it never runs.
    struct Pending
    {
        std::unique_ptr<Impl> impl;
        RenderTarget::Prepared prepared;
        CreatedCallback onDone;
    };
    const WGPUSurface compatible = prepared->getSurface();  // null offscreen: any adapter
    auto pending = std::make_shared<Pending>(std::move(impl), std::move(*prepared), std::move(onDone));

    Context::create(std::move(instance), compatible, params.powerPreference, [pending](Context::Result context) {
        auto impl = std::move(pending->impl);
        auto onDone = std::move(pending->onDone);
        if (!context) {
            onDone(std::unexpected(std::move(context.error())));
            return;
        }
        impl->context = std::move(*context);
        auto renderTarget = RenderTarget::create(std::move(pending->prepared), impl->context, impl->resources);
        if (!renderTarget) {
            onDone(std::unexpected(std::move(renderTarget.error())));
            return;
        }
        impl->renderTarget.emplace(std::move(*renderTarget));
        const WGPUTextureFormat format = impl->renderTarget->getFormat();
        impl->passes.emplace(Impl::Passes{
            .grid = Plot::GridPass(impl->context, impl->resources, format),
            .trades = TradesPass(impl->context, impl->resources, format),
            .columns = ColumnsPass(impl->context, impl->resources, format),
        });
        impl->setSettings(impl->settings, true);
        impl->profiling.profiler = Profiler::create(impl->context,
            [&statsQueue = impl->profiling.statsQueue](std::uint64_t frame, Profiler::Times times) {
                statsQueue.setTimes(frame, std::move(times));
            });
        onDone(std::unique_ptr<Engine>(new Engine(std::move(impl))));
    });
}

Engine::Impl::~Impl()
{
    // Releasing the device loses it too, which isn't news.
    if (context.health) {
        context.health->released = true;
        context.health->onLost = nullptr;
    }
}

// Frames

void Engine::Impl::setSettings(const Settings& next, bool everything)
{
    const auto changed = [&](auto member) { return everything || settings.*member != next.*member; };
    renderTarget->setSize(next.widthPx, next.heightPx);
    if (changed(&Settings::pixelRatio)) {
        passes->trades.setPixelRatio(next.pixelRatio);
        passes->columns.setPixelRatio(next.pixelRatio);
    }
    if (changed(&Settings::grid)) {
        passes->grid.setGrid(next.grid);
    }
    if (changed(&Settings::view)) {
        passes->trades.setView(next.view);
        passes->columns.setView(next.view);
    }
    if (changed(&Settings::markerSizing)) {
        passes->trades.setMarkerSizing(next.markerSizing);
    }
    if (changed(&Settings::theme)) {
        passes->grid.setTheme(next.theme.plot);
        passes->trades.setTheme(next.theme);
        passes->columns.setTheme(next.theme);
    }
    if (changed(&Settings::highlightedColumn)) {
        passes->columns.setHighlight(next.highlightedColumn);
    }
    if (changed(&Settings::highlightedTrade)) {
        passes->trades.setHighlight(next.highlightedTrade);
    }
    // The insets are read as each frame is drawn.
    if (everything || settings != next) {
        settings = next;
        dirty = true;
    }
}

void Engine::Impl::render()
{
    const bool timed = Core::Profiling::Compiled && profiling.enabled;
    Gpu::Type::FrameStats::Cpu cpu{};
    const bool drawn = renderTarget->draw([&](WGPUCommandEncoder encoder, WGPUTextureView view) {
        profiling.profiler->beginFrame(timed);
        const Core::Profiling::Scope scope(cpu.encodeMs, timed);
        const auto plot = Plot::Area::within(renderTarget->getWidthPx(), renderTarget->getHeightPx(), settings.insets);
        passes->grid.encode(encoder, view, plot, *profiling.profiler);
        if (showColumns) {
            passes->columns.encode(encoder, view, plot, *profiling.profiler);
        } else {
            passes->trades.encode(encoder, view, plot, *profiling.profiler);
        }
        profiling.profiler->endPasses(encoder);
    }, cpu, timed);
    if (!drawn) {
        // Skipped frames stay needed.
        return;
    }
    dirty = false;

    profiling.statsQueue.push({
        .frame = ++profiling.lastFrame,
        .cpu = timed ? std::optional(cpu) : std::nullopt,
        .gpu = std::unexpected(Gpu::Type::FrameStats::UntimedReason::Undefined),
        .buffers = resources.getCount(Resource::Kind::Buffer),
        .textures = resources.getCount(Resource::Kind::Texture),
        .gpuBytes = resources.getTotalBytes(),
    });
    profiling.profiler->endFrame(profiling.lastFrame);
}

// Public interface

Engine::Engine(std::unique_ptr<Impl> impl) noexcept
    : mImpl(std::move(impl))
{}

Engine::~Engine() = default;

const Gpu::Type::Info& Engine::getInfo() const
{
    return mImpl->context.info;
}

void Engine::setSettings(const Settings& settings)
{
    mImpl->setSettings(settings, false);
}

void Engine::setColumns(const std::optional<Type::Columns>& columns)
{
    mImpl->showColumns = columns.has_value();
    if (columns) {
        mImpl->passes->columns.setColumns(*columns);
    }
    mImpl->dirty = true;
}

std::size_t Engine::getMaxTrades() const
{
    return std::min(mImpl->passes->trades.getMaxCount(), mImpl->maxTrades.value_or(std::numeric_limits<std::size_t>::max()));
}

std::expected<void, std::string> Engine::setTrades(const Core::Data::Trades& trades)
{
    if (trades.size() > getMaxTrades()) {
        return tooManyTrades(trades.size(), getMaxTrades());
    }
    auto& pass = mImpl->passes->trades;
    pass.clear();
    mImpl->dirty = true;
    // Can't fail below getMaxTrades(), but the buffer has the last word.
    if (auto stored = pass.reserve(trades.size()).and_then([&] { return pass.append(trades); }); !stored) {
        pass.clear();
        return std::unexpected(std::format("Failed to upload {} trades: {}", trades.size(), stored.error()));
    }
    return {};
}

std::expected<void, std::string> Engine::appendTrades(const Core::Data::Trades& trades)
{
    const std::size_t total = mImpl->passes->trades.getCount() + trades.size();
    if (total > getMaxTrades()) {
        return tooManyTrades(total, getMaxTrades());
    }
    if (auto appended = mImpl->passes->trades.append(trades); !appended) {
        return std::unexpected(std::format("Failed to upload {} trades: {}", trades.size(), appended.error()));
    }
    mImpl->dirty = true;
    return {};
}

void Engine::reserveTrades(std::size_t count)
{
    // Can't fail: no more than the buffer takes.
    [[maybe_unused]] const auto reserved = mImpl->passes->trades.reserve(std::min(count, getMaxTrades()));
}

void Engine::update(double nowMs)
{
    mImpl->nowMs = nowMs;
}

bool Engine::needsRender() const
{
    return mImpl->dirty;
}

void Engine::render()
{
    mImpl->render();
}

void Engine::setProfiling(bool enabled)
{
    mImpl->profiling.enabled = enabled;
}

void Engine::setStatsCallback(const StatsCallback& onStats)
{
    mImpl->profiling.statsQueue.setCallback(onStats);
}

#ifndef __EMSCRIPTEN__
void Engine::processEvents()
{
    wgpuInstanceProcessEvents(mImpl->context.instance.get());
}
#endif

void Engine::readPixels(PixelsCallback onDone)
{
    if (mImpl->renderTarget->readPixels(std::move(onDone))) {
        mImpl->dirty = true;
    }
}

// Health

const Gpu::Type::Health& Engine::getHealth() const
{
    return *mImpl->context.health;
}

void Engine::setLostCallback(std::function<void()> onLost)
{
    mImpl->context.health->onLost = std::move(onLost);
}

void Engine::loseDevice()
{
    wgpuDeviceDestroy(mImpl->context.device.get());
}

std::uint64_t Engine::getGpuBytes() const
{
    return mImpl->resources.getTotalBytes();
}

} // namespace Gpu::Market
