#pragma once

#include "Gpu/Market/Engine.hpp"
#include "Gpu/Type.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace Viewer {

/**
 * The platform-independent application. The platform entry point
 * (NativeMain.cpp or WebMain.cpp) owns the loop and calls
 * resize/update/render.
 */
class App
{
public:
    using Result = std::expected<std::unique_ptr<App>, std::string>;
    using Callback = std::function<void(Result)>;

    /** Calls `onDone` exactly once, possibly before returning. */
    static void create(const Gpu::Type::Target::Any& target, Gpu::Type::PowerPreference powerPreference, Callback onDone);

    explicit App(std::unique_ptr<Gpu::Market::Engine> engine) noexcept;

    /** A one-line description of the GPU in use, for display. */
    [[nodiscard]] std::string describe() const;

    /** Surface size in physical pixels. */
    void resize(std::uint32_t width, std::uint32_t height);

    /** Advances application state. CPU only. */
    void update(double nowMs);

    /** Draws the current state. Does not change it. */
    void render();

private:
    std::unique_ptr<Gpu::Market::Engine> mEngine;
    double mNowMs = 0.0;
};

} // namespace Viewer
