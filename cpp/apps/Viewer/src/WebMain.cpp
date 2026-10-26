#include "App.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <memory>
#include <string>
#include <utility>

namespace {

using emscripten::val;

/** Accepts the WebGPU spelling: "low-power", "high-performance", anything else is the default. */
Graphics::Type::PowerPreference parsePowerPreference(const std::string& value)
{
    if (value == "low-power") {
        return Graphics::Type::PowerPreference::LowPower;
    }
    if (value == "high-performance") {
        return Graphics::Type::PowerPreference::HighPerformance;
    }
    return Graphics::Type::PowerPreference::Default;
}

/**
 * The object JavaScript holds. JavaScript owns the frame loop and must
 * call delete() when done.
 */
class WebViewer
{
public:
    /** Calls `onDone(error)` once: `null` on success, a string on failure. */
    void init(std::string selector, std::string powerPreference, val onDone)
    {
        std::weak_ptr<std::unique_ptr<Viewer::App>> slot = mApp;
        Viewer::App::create(Graphics::Type::Target::Canvas{ std::move(selector) }, parsePowerPreference(powerPreference),
            [slot, onDone](Viewer::App::Result result) {
                auto app = slot.lock();
                if (!app) {
                    // delete() was called before initialization finished.
                    return;
                }
                if (!result) {
                    onDone(result.error());
                    return;
                }
                *app = std::move(*result);
                onDone(val::null());
            });
    }

    std::string describe() const
    {
        return *mApp ? (*mApp)->describe() : std::string{};
    }

    void resize(std::uint32_t width, std::uint32_t height)
    {
        if (*mApp) {
            (*mApp)->resize(width, height);
        }
    }

    void update(double nowMs)
    {
        if (*mApp) {
            (*mApp)->update(nowMs);
        }
    }

    void render()
    {
        if (*mApp) {
            (*mApp)->render();
        }
    }

private:
    std::shared_ptr<std::unique_ptr<Viewer::App>> mApp = std::make_shared<std::unique_ptr<Viewer::App>>();
};

} // namespace

EMSCRIPTEN_BINDINGS(Viewer)
{
    emscripten::class_<WebViewer>("Viewer")
        .constructor<>()
        .function("init", &WebViewer::init)
        .function("describe", &WebViewer::describe)
        .function("resize", &WebViewer::resize)
        .function("update", &WebViewer::update)
        .function("render", &WebViewer::render);
}
