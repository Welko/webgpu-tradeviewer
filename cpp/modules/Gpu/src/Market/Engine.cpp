#include "Gpu/Market/Engine.hpp"

#include "Gpu/Engine.hpp"
#include "Gpu/Pass/Clear.hpp"

#include <webgpu/webgpu.h>

#include <utility>

namespace Gpu::Market {

namespace {

constexpr WGPUColor ClearColor{ 0.07, 0.08, 0.10, 1.0 };

} // namespace

struct Engine::Impl
{
    // The generic core. Market data the passes share will be declared after
    // it, so that data is destroyed first.
    Gpu::Engine engine;
};

void Engine::create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone)
{
    Gpu::Engine::create(target, powerPreference, [onDone = std::move(onDone)](Gpu::Engine::Result engine) {
        if (!engine) {
            onDone(std::unexpected(std::move(engine.error())));
            return;
        }
        engine->addPass(std::make_unique<Pass::Clear>(ClearColor));
        onDone(std::unique_ptr<Engine>(new Engine(std::make_unique<Impl>(std::move(*engine)))));
    });
}

Engine::Engine(std::unique_ptr<Impl> impl) noexcept
    : mImpl(std::move(impl))
{}

Engine::~Engine() = default;

const Gpu::Type::Info& Engine::info() const
{
    return mImpl->engine.info();
}

void Engine::resize(std::uint32_t width, std::uint32_t height)
{
    mImpl->engine.resize(width, height);
}

void Engine::update(double nowMs)
{
    mImpl->engine.update(nowMs);
}

bool Engine::needsRender() const
{
    return mImpl->engine.needsRender();
}

void Engine::render()
{
    mImpl->engine.render();
}

} // namespace Gpu::Market
