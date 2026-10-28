#include "App.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <memory>
#include <string>
#include <utility>

namespace {

using emscripten::val;

/** Accepts the WebGPU spelling: "low-power", "high-performance", anything else is the default. */
Gpu::Type::PowerPreference parsePowerPreference(const std::string& value)
{
    if (value == "low-power") {
        return Gpu::Type::PowerPreference::LowPower;
    }
    if (value == "high-performance") {
        return Gpu::Type::PowerPreference::HighPerformance;
    }
    return Gpu::Type::PowerPreference::Default;
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
        Viewer::App::create(Gpu::Type::Target::Canvas{ .selector = std::move(selector) }, parsePowerPreference(powerPreference),
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

    /** The GPU in use, as a plain JavaScript object. Empty until init() succeeds. */
    Gpu::Type::Info info() const
    {
        return *mApp ? (*mApp)->info() : Gpu::Type::Info{};
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

    bool needsRender() const
    {
        return *mApp && (*mApp)->needsRender();
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
    emscripten::value_object<Gpu::Type::Info>("GpuInfo")
        .field("vendor", &Gpu::Type::Info::vendor)
        .field("device", &Gpu::Type::Info::device)
        .field("architecture", &Gpu::Type::Info::architecture)
        .field("description", &Gpu::Type::Info::description)
        .field("backend", &Gpu::Type::Info::backend)
        .field("adapter", &Gpu::Type::Info::adapter)
        .field("webglRenderer", &Gpu::Type::Info::webglRenderer);

    emscripten::class_<WebViewer>("Viewer")
        .constructor<>()
        .function("init", &WebViewer::init)
        .function("info", &WebViewer::info)
        .function("resize", &WebViewer::resize)
        .function("update", &WebViewer::update)
        .function("needsRender", &WebViewer::needsRender)
        .function("render", &WebViewer::render);
}
