#include "Gpu/Resource.hpp"

#include <doctest/doctest.h>

TEST_CASE("New stats are empty")
{
    const Gpu::Resource::Stats stats;
    CHECK(stats.empty());
    CHECK(stats.getTotalBytes() == 0);
}

TEST_CASE("Stats count objects and bytes per kind until they are removed")
{
    Gpu::Resource::Stats stats;
    stats.add(Gpu::Resource::Kind::Buffer, 256);
    stats.add(Gpu::Resource::Kind::Texture, 1024);
    stats.add(Gpu::Resource::Kind::Texture, 1024);
    CHECK_FALSE(stats.empty());
    CHECK(stats.getCount(Gpu::Resource::Kind::Texture) == 2);
    CHECK(stats.getBytes(Gpu::Resource::Kind::Texture) == 2048);
    CHECK(stats.getCount(Gpu::Resource::Kind::Buffer) == 1);
    CHECK(stats.getBytes(Gpu::Resource::Kind::Buffer) == 256);
    CHECK(stats.getTotalBytes() == 2304);

    stats.remove(Gpu::Resource::Kind::Texture, 1024);
    stats.remove(Gpu::Resource::Kind::Texture, 1024);
    stats.remove(Gpu::Resource::Kind::Buffer, 256);
    CHECK(stats.empty());
    CHECK(stats.getTotalBytes() == 0);
}
