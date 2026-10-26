#pragma once

#include "Graphics/Type.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace Graphics {

/**
 * The public face of Graphics: owns the WebGPU context and everything drawn
 * with it. No WebGPU types appear here.
 *
 * For now it only clears the surface. Drawing layers (trades, heatmap...)
 * will live behind it.
 */
class Renderer
{
public:
    using Result = std::expected<std::unique_ptr<Renderer>, std::string>;
    using Callback = std::function<void(Result)>;

    /**
     * Calls `onDone` exactly once: before returning (wgpu-native) or later
     * from the event loop (web).
     */
    static void create(const Type::Target::Any& target, Type::PowerPreference powerPreference, Callback onDone);

    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] const Type::Info& getInfo() const;

    /** Surface size in physical pixels. Zero sizes are ignored. */
    void resize(std::uint32_t width, std::uint32_t height);

    void render();

private:
    struct Impl;

    explicit Renderer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> mImpl;
};

} // namespace Graphics
