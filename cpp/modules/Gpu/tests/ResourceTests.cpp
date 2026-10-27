#include "Gpu/Resource.hpp"

#include <doctest/doctest.h>

TEST_CASE("New stats are empty")
{
    const Gpu::Resource::Stats stats;
    CHECK(stats.empty());
    CHECK(stats.totalBytes() == 0);
}

TEST_CASE("Stats count objects and bytes per kind until they are removed")
{
    Gpu::Resource::Stats stats;
    stats.add(Gpu::Resource::Kind::Surface, 0);
    stats.add(Gpu::Resource::Kind::Texture, 1024);
    stats.add(Gpu::Resource::Kind::Texture, 1024);
    CHECK_FALSE(stats.empty());
    CHECK(stats.count(Gpu::Resource::Kind::Texture) == 2);
    CHECK(stats.bytes(Gpu::Resource::Kind::Texture) == 2048);
    CHECK(stats.count(Gpu::Resource::Kind::Surface) == 1);
    CHECK(stats.bytes(Gpu::Resource::Kind::Surface) == 0);
    CHECK(stats.totalBytes() == 2048);

    stats.remove(Gpu::Resource::Kind::Texture, 1024);
    stats.remove(Gpu::Resource::Kind::Texture, 1024);
    stats.remove(Gpu::Resource::Kind::Surface, 0);
    CHECK(stats.empty());
    CHECK(stats.totalBytes() == 0);
}
