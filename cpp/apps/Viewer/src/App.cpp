#include "App.hpp"

#include <utility>

namespace Viewer {

void App::create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone)
{
    Gpu::Market::Engine::create(target, powerPreference, [onDone = std::move(onDone)](Gpu::Market::Engine::Result result) {
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

std::string App::describe() const
{
    const auto& info = mEngine->getInfo();

    // Browsers usually hide `device`, but expose `vendor` and `architecture`.
    std::string name = info.device;
    if (name.empty()) {
        name = info.vendor;
        if (!info.architecture.empty()) {
            name += (name.empty() ? "" : " ") + info.architecture;
        }
    }
    if (name.empty() && !info.webglRenderer.empty()) {
        name = info.webglRenderer + " (via WebGL)";
    }
    if (name.empty()) {
        name = "Unknown GPU (details hidden)";
    }

    name += " · " + info.backend;
    if (info.adapter != "Unknown") {
        name += " · " + info.adapter;
    }
    return name;
}

void App::resize(std::uint32_t width, std::uint32_t height)
{
    mEngine->resize(width, height);
}

void App::update(double nowMs)
{
    mNowMs = nowMs;
}

void App::render()
{
    mEngine->render();
}

} // namespace Viewer
