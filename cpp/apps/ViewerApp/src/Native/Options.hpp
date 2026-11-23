#pragma once

#include "Gpu/Type.hpp"

#include <optional>
#include <span>
#include <string>

namespace Viewer::Native {

/**
 * The command line:
 * ViewerApp [trades file] [--gpu=low-power|high-performance] [--scene=<name>] [--snapshot=<file.png>] [--list-scenes]
 */
struct Options
{
    /** A trades file to show instead of the synthetic day. */
    std::optional<std::string> tradesPath;
    Gpu::Type::PowerPreference powerPreference = Gpu::Type::PowerPreference::Default;
    /** An image-test scene to draw instead of the trades, once, at its own size. */
    std::optional<std::string> sceneName;
    /** Where to save the first frame as a PNG, then exit. */
    std::optional<std::string> snapshotPath;
    /** Print the scenes' names, one per line, instead (for tools/render-native.sh). */
    bool listScenes = false;
};

/** The options in `args`, without the program's name. Unknown ones are ignored. */
[[nodiscard]] Options parseOptions(std::span<const char* const> args);

}  // namespace Viewer::Native
