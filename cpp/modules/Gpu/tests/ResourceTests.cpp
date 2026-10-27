#include "Gpu/Resource.hpp"

#include <doctest/doctest.h>

TEST_CASE("Empty stats describe nothing")
{
    const Gpu::Resource::Stats stats;
    CHECK(stats.empty());
    CHECK(stats.describe() == "nothing");
}

TEST_CASE("Stats describe what is alive, with bytes only where there are any")
{
    Gpu::Resource::Stats stats;
    stats.add(Gpu::Resource::Kind::Surface, 0);
    stats.add(Gpu::Resource::Kind::Texture, 1024);
    stats.add(Gpu::Resource::Kind::Texture, 1024);
    CHECK_FALSE(stats.empty());
    CHECK(stats.describe() == "textures: 2 (2048 bytes), surfaces: 1");

    stats.remove(Gpu::Resource::Kind::Texture, 1024);
    stats.remove(Gpu::Resource::Kind::Texture, 1024);
    stats.remove(Gpu::Resource::Kind::Surface, 0);
    CHECK(stats.empty());
}
