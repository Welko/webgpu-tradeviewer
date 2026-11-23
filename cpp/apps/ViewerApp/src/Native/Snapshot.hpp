#pragma once

#include "Viewer/Scene.hpp"

#include "Gpu/Type.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace Viewer::Native {

/**
 * The first frame, saved as a PNG for --snapshot. A scene's known pixels
 * aren't checked here: tools/compare-renderings.mjs checks them on every
 * platform.
 */
class Snapshot
{
public:
    explicit Snapshot(std::string path);

    /** Whether the frame has arrived, saved or not. */
    [[nodiscard]] bool isDone() const { return mDone; }
    [[nodiscard]] bool isWritten() const { return mWritten; }

    /** Saves `pixels`, RGBA rows of `widthPx` × `heightPx`, or logs why it can't. */
    void write(std::uint32_t widthPx, std::uint32_t heightPx, const std::expected<std::vector<std::uint8_t>, std::string>& pixels);

    /**
     * Beside the PNG: `scene`'s known pixels, `patches`, in <name>.json, and
     * what drew it, `gpu`, in about.json, which every scene's run rewrites.
     * As web/tests/scenes.spec.ts writes them for the browsers.
     */
    void writeSceneFiles(const Scene& scene, const std::vector<Scene::Patch>& patches, const Gpu::Type::Info& gpu) const;

private:
    std::string mPath;
    bool mDone = false;
    bool mWritten = false;
};

}  // namespace Viewer::Native
