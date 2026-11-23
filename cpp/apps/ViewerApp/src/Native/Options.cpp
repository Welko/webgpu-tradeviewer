#include "Native/Options.hpp"

#include <string_view>

namespace Viewer::Native {

Options parseOptions(std::span<const char* const> args)
{
    Options options;
    for (const std::string_view arg : args)
    {
        if (arg == "--gpu=low-power")
        {
            options.powerPreference = Gpu::Type::PowerPreference::LowPower;
        }
        else if (arg == "--gpu=high-performance")
        {
            options.powerPreference = Gpu::Type::PowerPreference::HighPerformance;
        }
        else if (arg.starts_with("--scene="))
        {
            options.sceneName = std::string(arg.substr(std::string_view("--scene=").size()));
        }
        else if (arg.starts_with("--snapshot="))
        {
            options.snapshotPath = std::string(arg.substr(std::string_view("--snapshot=").size()));
        }
        else if (arg == "--list-scenes")
        {
            options.listScenes = true;
        }
        else if (!arg.starts_with("--") && !options.tradesPath)
        {
            options.tradesPath = std::string(arg);
        }
    }
    return options;
}

}  // namespace Viewer::Native
