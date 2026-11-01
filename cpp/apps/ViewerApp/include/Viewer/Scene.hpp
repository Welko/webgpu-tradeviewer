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
    /** A pixel, or an area whose every pixel, has a color known in advance. */
    struct Patch
    {
        std::string what;
        std::uint32_t xPx;  // its top left corner, from the surface's top left corner
        std::uint32_t yPx;
        std::uint32_t widthPx = 1;
        std::uint32_t heightPx = 1;
        std::array<std::uint8_t, 4> rgba;
        /** How far each channel may be off: 0 for flat colors, 1 for blended ones, which GPUs round differently. */
        int tolerance;
    };

    std::string_view name;
    std::string_view description;
    std::uint32_t widthPx;
    std::uint32_t heightPx;
    Gpu::Plot::Type::Insets insets;
    /** Known whatever the data: the margins, and what the scene placed itself. */
    std::vector<Patch> patches;
    /** Sets the trades, colors, view and pointer, after show() sets the size. */
    void (*setUp)(App& app);
    /** Adds what's known only once set up, from where the app drew things (grid lines, the ring...); null for nothing. */
    void (*findPatches)(const Scene& scene, const App& app, std::vector<Patch>& patches) = nullptr;

    /** Every scene, in the order reports show them. */
    [[nodiscard]] static std::span<const Scene> getScenes();

    [[nodiscard]] static const Scene* find(std::string_view name);

    /** Shows `scene` in `app`, which must draw to a surface of the scene's size. Returns its known pixels. */
    static std::vector<Patch> show(const Scene& scene, App& app);

    /** One line for each of `patches` that's off in `rgba` (rows of the scene's size, top first). */
    [[nodiscard]] static std::vector<std::string> checkPatches(const Scene& scene, std::span<const Patch> patches,
                                                               std::span<const std::uint8_t> rgba);
};

} // namespace Viewer
