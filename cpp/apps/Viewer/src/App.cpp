#include "App.hpp"

#include <utility>

namespace Viewer {

void App::create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone)
{
    Gpu::Market::Engine::create(target, powerPreference, [onDone = std::move(onDone)](Gpu::Market::Engine::CreatedResult result) {
        if (!result) {
            onDone(std::unexpected(std::move(result.error())));
            return;
        }
        onDone(std::make_unique<App>(std::move(*result)));
    });
}

App::App(std::unique_ptr<Gpu::Market::Engine> engine) noexcept
    : mEngine(std::move(engine))
{}

const Gpu::Type::Info& App::info() const
{
    return mEngine->info();
}

void App::resize(std::uint32_t width, std::uint32_t height)
{
    mEngine->resize(width, height);
}

void App::update(double nowMs)
{
    mEngine->update(nowMs);
}

bool App::needsRender() const
{
    return mEngine->needsRender();
}

void App::render()
{
    mEngine->render();
}

void App::setProfiling(bool enabled)
{
    mEngine->setProfiling(enabled);
}

void App::setStatsCallback(Gpu::Market::Engine::StatsCallback onStats)
{
    mEngine->setStatsCallback(std::move(onStats));
}

#ifndef __EMSCRIPTEN__
void App::processEvents()
{
    mEngine->processEvents();
}
#endif

} // namespace Viewer
