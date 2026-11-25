#include "EngineTesting.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <expected>
#include <string>
#include <thread>
#include <utility>

namespace EngineTesting {

std::unique_ptr<Engine> createOffscreen(std::uint32_t widthPx, std::uint32_t heightPx)
{
    std::unique_ptr<Engine> engine;
    bool done = false;
    Engine::create({ .target = Gpu::Type::Target::Offscreen{ .width = widthPx, .height = heightPx } }, [&](Engine::CreatedResult result) {
        done = true;
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result)
        {
            engine = std::move(*result);
        }
    });
    // Natively, creation always finishes before create() returns.
    CHECK_MESSAGE(done, "Engine creation didn't finish before create() returned");
    return engine;
}

std::optional<Pixels> readPixels(Engine& engine)
{
    std::optional<Pixels> pixels;
    bool done = false;
    engine.readPixels([&](std::expected<Pixels, std::string> result) {
        done = true;
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result)
        {
            pixels = std::move(*result);
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done && std::chrono::steady_clock::now() < deadline)
    {
        engine.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(done, "The readback never completed");
    return pixels;
}

void recordStats(Engine& engine, std::vector<FrameStats>& reported)
{
    engine.setStatsCallback([&reported](const FrameStats& stats) { reported.push_back(stats); });
}

bool waitForStats(Engine& engine, const std::vector<FrameStats>& reported, std::size_t count)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (reported.size() < count && std::chrono::steady_clock::now() < deadline)
    {
        engine.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_MESSAGE(reported.size() == count, "Not every frame was reported");
    return reported.size() == count;
}

bool isBackground(const Pixels& pixels)
{
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
    {
        if (std::abs(pixels[i] - 18) > 1 || std::abs(pixels[i + 1] - 20) > 1 || std::abs(pixels[i + 2] - 26) > 1 || pixels[i + 3] != 255)
        {
            return false;
        }
    }
    return !pixels.empty();
}

Rgba pixelAt(const Pixels& pixels, std::uint32_t widthPx, std::uint32_t xPx, std::uint32_t yPx)
{
    const std::size_t i = (std::size_t{ yPx } * widthPx + xPx) * 4;
    return { .red = pixels[i], .green = pixels[i + 1], .blue = pixels[i + 2], .alpha = pixels[i + 3] };
}

bool isBuy(Rgba pixel)
{
    return pixel.green > pixel.red + 50;
}

bool isSell(Rgba pixel)
{
    return pixel.red > pixel.green + 50;
}

bool isBackground(Rgba pixel)
{
    return std::abs(pixel.red - 18) <= 1 && std::abs(pixel.green - 20) <= 1 && std::abs(pixel.blue - 26) <= 1;
}

bool isGridLine(Rgba pixel)
{
    return pixel.red > 25 && pixel.red < 50 && std::abs(pixel.red - pixel.green) <= 3;
}

void add(Trades& trades, std::int64_t timeUs, double price, double amount, Side side)
{
    trades.timeUs.push_back(timeUs);
    trades.price.push_back(price);
    trades.amount.push_back(amount);
    trades.side.push_back(side);
}

Trades buyThenSell()
{
    Trades trades;
    add(trades, NoonUs + 25, 100.0, 1.0, Side::Buy);
    add(trades, NoonUs + 75, 100.0, 1.0, Side::Sell);
    return trades;
}

Gpu::Market::Type::View viewOf(Core::IndexRange trades)
{
    return { .time = { .startUs = NoonUs, .endUs = NoonUs + 100 }, .price = { .low = 99.0, .high = 101.0 }, .trades = trades };
}

}  // namespace EngineTesting
