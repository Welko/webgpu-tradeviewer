#include "EngineTesting.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <utility>
#include <vector>

using namespace EngineTesting;

TEST_CASE("An offscreen engine draws its background into every pixel")
{
    const auto engine = createOffscreen(64, 16);
    if (!engine)
    {
        return;
    }
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels)
    {
        return;
    }
    CHECK(pixels->size() == 64 * 16 * 4);
    CHECK(isBackground(*pixels));
}

TEST_CASE("Resizing an offscreen engine replaces its texture")
{
    const auto engine = createOffscreen(64, 16);
    if (!engine)
    {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    engine->render();
    engine->setSettings({ .widthPx = 32, .heightPx = 8 });
    CHECK(engine->needsRender());
    engine->render();
    CHECK_FALSE(engine->needsRender());

    const auto pixels = readPixels(*engine);
    if (!pixels)
    {
        return;
    }
    CHECK(pixels->size() == 32 * 8 * 4);
    CHECK(isBackground(*pixels));
    CHECK(reported.size() == 2);
    if (reported.size() == 2)
    {
        CHECK(reported[1].textures == 1);
        CHECK(reported[0].gpuBytes - reported[1].gpuBytes == (64 * 16 - 32 * 8) * 4);
    }
}

TEST_CASE("An offscreen target needs a size")
{
    std::optional<Engine::CreatedResult> result;
    Engine::create({ .target = Gpu::Type::Target::Offscreen{ .width = 0, .height = 16 } },
                   [&](Engine::CreatedResult created) { result = std::move(created); });
    CHECK(result);
    if (result)
    {
        CHECK_FALSE(*result);
    }
}
