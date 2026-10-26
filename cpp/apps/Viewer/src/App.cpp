#include "App.hpp"

#include <utility>

namespace Viewer {

void App::create(const Graphics::Type::Target::Any& target, Graphics::Type::PowerPreference powerPreference, Callback onDone)
{
    Graphics::Renderer::create(target, powerPreference, [onDone = std::move(onDone)](Graphics::Renderer::Result result) {
        if (!result) {
            onDone(std::unexpected(std::move(result.error())));
            return;
        }
        onDone(std::make_unique<App>(std::move(*result)));
    });
}

App::App(std::unique_ptr<Graphics::Renderer> renderer)
    : mRenderer(std::move(renderer))
{}

std::string App::describe() const
{
    const auto& info = mRenderer->getInfo();

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
    mRenderer->resize(width, height);
}

void App::update(double nowMs)
{
    mNowMs = nowMs;
}

void App::render()
{
    mRenderer->render();
}

} // namespace Viewer
