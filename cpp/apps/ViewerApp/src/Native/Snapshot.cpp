#include "Native/Snapshot.hpp"

#include "Native/Png.hpp"

#include "Core/Log.hpp"

#include <nlohmann/json.hpp>

#ifdef __linux__
#include <sys/utsname.h>
#endif

#include <filesystem>
#include <format>
#include <fstream>
#include <utility>

namespace Viewer::Native {

namespace {

nlohmann::ordered_json getSceneJson(const Scene& scene, const std::vector<Scene::Patch>& patches)
{
    auto list = nlohmann::ordered_json::array();
    for (const auto& patch : patches)
    {
        list.push_back({
            { "what", patch.what },
            { "xPx", patch.xPx },
            { "yPx", patch.yPx },
            { "widthPx", patch.widthPx },
            { "heightPx", patch.heightPx },
            { "rgba", patch.rgba },
            { "tolerance", patch.tolerance },
        });
    }
    nlohmann::ordered_json json;
    json["name"] = scene.name;
    json["description"] = scene.description;
    json["widthPx"] = scene.widthPx;
    json["heightPx"] = scene.heightPx;
    json["patches"] = std::move(list);
    return json;
}

/** What the comparison report says about this platform. */
nlohmann::ordered_json getAboutJson(const Gpu::Type::Info& gpu)
{
#ifdef __linux__
    utsname system{};
    uname(&system);
    const std::string os = std::format("{} {}", system.sysname, system.machine);
#else
#error "getAboutJson() names the system on Linux only"
#endif
    auto scenes = nlohmann::ordered_json::array();
    for (const auto& scene : Scene::getScenes())
    {
        scenes.push_back(scene.name);
    }
    return {
        { "platform", "native" },
        { "version", std::format("wgpu-native {}, {}", TRADEVIEWER_WGPU_VERSION, os) },
        { "gpu", std::format("{} · {} · {}", gpu.device, gpu.backend, gpu.adapter) },
        { "scenes", std::move(scenes) },
    };
}

/** Writes `json` as the browsers' tests do: indented by 2, with a final newline. */
void writeJson(const std::filesystem::path& path, const nlohmann::ordered_json& json)
{
    if (!(std::ofstream(path) << json.dump(2) << '\n'))
    {
        Core::Log::error("Failed to write {}", path.string());
    }
}

}  // namespace

Snapshot::Snapshot(std::string path)
    : mPath(std::move(path))
{}

void Snapshot::write(std::uint32_t widthPx, std::uint32_t heightPx, const std::expected<std::vector<std::uint8_t>, std::string>& pixels)
{
    mDone = true;
    if (!pixels)
    {
        Core::Log::error("{}", pixels.error());
        return;
    }
    if (auto written = Png::write(mPath, widthPx, heightPx, *pixels); !written)
    {
        Core::Log::error("{}", written.error());
        return;
    }
    mWritten = true;
    Core::Log::info("Wrote {}", mPath);
}

void Snapshot::writeSceneFiles(const Scene& scene, const std::vector<Scene::Patch>& patches, const Gpu::Type::Info& gpu) const
{
    const std::filesystem::path path = mPath;
    writeJson(std::filesystem::path(path).replace_extension(".json"), getSceneJson(scene, patches));
    writeJson(path.parent_path() / "about.json", getAboutJson(gpu));
}

}  // namespace Viewer::Native
