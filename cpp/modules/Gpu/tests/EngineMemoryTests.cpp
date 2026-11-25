#include "EngineTesting.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using namespace EngineTesting;

TEST_CASE("Appending past the reserved room keeps the earlier trades")
{
    const auto engine = createOffscreen(100, 50);
    if (!engine)
    {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    const auto both = buyThenSell();
    Trades first;
    add(first, both.timeUs[0], both.price[0], both.amount[0], both.side[0]);
    Trades second;
    add(second, both.timeUs[1], both.price[1], both.amount[1], both.side[1]);

    engine->reserveTrades(1);
    CHECK(engine->appendTrades(first));
    engine->render();
    CHECK(engine->appendTrades(second));
    engine->setSettings({ .view = viewOf({ .first = 0, .last = 2 }) });
    engine->render();

    const auto pixels = readPixels(*engine);
    if (!pixels || reported.size() != 2)
    {
        CHECK(reported.size() == 2);
        return;
    }
    CHECK(isBuy(pixelAt(*pixels, 100, 25, 25)));
    CHECK(isSell(pixelAt(*pixels, 100, 75, 25)));
    // The bigger buffer replaced the first: as many buffers, more bytes.
    CHECK(reported[1].buffers == reported[0].buffers);
    CHECK(reported[1].gpuBytes > reported[0].gpuBytes);
}

TEST_CASE("Zooming and panning allocate nothing")
{
    const auto engine = createOffscreen(100, 50);
    if (!engine)
    {
        return;
    }
    std::vector<FrameStats> reported;
    recordStats(*engine, reported);
    CHECK(engine->setTrades(buyThenSell()));
    for (std::int64_t step = 0; step < 50; ++step)
    {
        engine->setSettings({ .view = { .time = { .startUs = NoonUs + step, .endUs = NoonUs + 100 + step * 3 },
                                        .price = { .low = 99.0, .high = 101.0 },
                                        .trades = { .first = 0, .last = 2 } } });
        engine->render();
    }
    CHECK(reported.size() == 50);
    for (const auto& stats : reported)
    {
        CHECK(stats.buffers == reported.front().buffers);
        CHECK(stats.textures == reported.front().textures);
        CHECK(stats.gpuBytes == reported.front().gpuBytes);
    }
}
