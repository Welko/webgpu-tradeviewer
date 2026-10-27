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
    stats.add(Gpu::Resource::Kind::Buffer, 256);
    stats.add(Gpu::Resource::Kind::Texture, 1024);
    stats.add(Gpu::Resource::Kind::Texture, 1024);
    CHECK_FALSE(stats.empty());
    CHECK(stats.count(Gpu::Resource::Kind::Texture) == 2);
    CHECK(stats.bytes(Gpu::Resource::Kind::Texture) == 2048);
    CHECK(stats.count(Gpu::Resource::Kind::Buffer) == 1);
    CHECK(stats.bytes(Gpu::Resource::Kind::Buffer) == 256);
    CHECK(stats.totalBytes() == 2304);

    stats.remove(Gpu::Resource::Kind::Texture, 1024);
    stats.remove(Gpu::Resource::Kind::Texture, 1024);
    stats.remove(Gpu::Resource::Kind::Buffer, 256);
    CHECK(stats.empty());
    CHECK(stats.totalBytes() == 0);
}
