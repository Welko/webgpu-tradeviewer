#pragma once

#include "Viewer/App.hpp"

#include "Gpu/Plot/Type.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * Fixed pictures for image tests: the same data, size and colors on every
 * platform, so their renderings can be compared with each other and with
 * colors known in advance.
 */
namespace Viewer {

struct Scene
{
    /** A pixel whose color is known in advance. */
    struct Patch
    {
        std::string_view what;
        std::uint32_t xPx;  // from the surface's top left corner
        std::uint32_t yPx;
        std::array<std::uint8_t, 4> rgba;
        /** How far each channel may be off: 0 for flat colors, 1 for blended ones, which GPUs round differently. */
        int tolerance;
    };

    std::string_view name;
    std::string_view description;
    std::uint32_t widthPx;
    std::uint32_t heightPx;
    Gpu::Plot::Type::Insets insets;
    std::vector<Patch> patches;
    /** Sets the trades, colors, view and pointer, after show() sets the size. */
    void (*setUp)(App& app);

    /** Every scene, in the order reports show them. */
    [[nodiscard]] static std::span<const Scene> getScenes();

    [[nodiscard]] static const Scene* find(std::string_view name);

    /** Shows `scene` in `app`, which must draw to a surface of the scene's size. */
    static void show(const Scene& scene, App& app);

    /** One line for each patch whose pixel in `rgba` (rows of the scene's size, top first) is off. */
    [[nodiscard]] static std::vector<std::string> checkPatches(const Scene& scene, std::span<const std::uint8_t> rgba);
};

} // namespace Viewer
