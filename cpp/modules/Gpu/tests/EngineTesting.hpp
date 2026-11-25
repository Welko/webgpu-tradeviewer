#pragma once

#include "Gpu/Market/Engine.hpp"

#include "Core/Data/Trades.hpp"
#include "Core/Range.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

/**
 * What the Engine tests share: an offscreen engine, its pixels and stats, and
 * a few trades to draw. Built without exceptions: CHECK and return early,
 * instead of REQUIRE.
 */
namespace EngineTesting {

using Gpu::Market::Engine;
using Pixels = Engine::Pixels;
using Core::Data::Side;
using Core::Data::Trades;
using Gpu::Type::FrameStats;

inline constexpr std::int64_t NoonUs = 1'575'201'600'000'000;  // 2019-12-01 12:00 UTC

/** Null, with a failed check, if no engine could be created. */
std::unique_ptr<Engine> createOffscreen(std::uint32_t widthPx, std::uint32_t heightPx);

/** The offscreen pixels, or nothing, with a failed check, if they don't arrive in 5 s. */
std::optional<Pixels> readPixels(Engine& engine);

/** Collects every frame's stats as the engine reports them. */
void recordStats(Engine& engine, std::vector<FrameStats>& reported);

/** Pumps events until `count` frames have been reported. False, with a failed check, if that takes 5 s. */
bool waitForStats(Engine& engine, const std::vector<FrameStats>& reported, std::size_t count);

/** Whether every pixel is the engine's background, (0.07, 0.08, 0.10) in 8-bit RGBA. */
bool isBackground(const Pixels& pixels);

struct Rgba
{
    int red;
    int green;
    int blue;
    int alpha;
};

/** The pixel at column `x` and row `y`, counting rows from the top. */
Rgba pixelAt(const Pixels& pixels, std::uint32_t widthPx, std::uint32_t xPx, std::uint32_t yPx);

bool isBuy(Rgba pixel);
bool isSell(Rgba pixel);
bool isBackground(Rgba pixel);

/** A faint gray grid line over the background. */
bool isGridLine(Rgba pixel);

/** Adds a trade at `timeUs`. */
void add(Trades& trades, std::int64_t timeUs, double price, double amount, Side side);

/** A buy a quarter of the way along, mid-height, and a sell three quarters along. */
Trades buyThenSell();

/** 100 µs from noon, prices 99 to 101, showing `trades`. */
Gpu::Market::Type::View viewOf(Core::IndexRange trades);

}  // namespace EngineTesting
