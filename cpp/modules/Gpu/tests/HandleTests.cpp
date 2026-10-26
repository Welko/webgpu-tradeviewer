#include "Gpu/Handle.hpp"
#include "Gpu/Resource.hpp"

#include <doctest/doctest.h>

#include <utility>

namespace {

// Stands in for a WebGPU handle type, so these tests need no GPU.
struct FakeObject
{
    int releases = 0;
};
using FakeHandle = FakeObject*;

// Same shape as a WebGPU type, but with no Resource::Kind: never trackable.
struct FakeRootObject
{
    int releases = 0;
};
using FakeRootHandle = FakeRootObject*;

} // namespace

template <>
struct Gpu::Handle<FakeHandle>::Traits
{
    static constexpr Gpu::Resource::Kind Kind = Gpu::Resource::Kind::Buffer;
    static void release(FakeHandle raw) { ++raw->releases; }
};

template <>
struct Gpu::Handle<FakeRootHandle>::Traits
{
    static void release(FakeRootHandle raw) { ++raw->releases; }
};

TEST_CASE("Handle releases its object exactly once")
{
    FakeObject object;
    {
        const Gpu::Handle<FakeHandle> handle(&object);
        CHECK(handle.get() == &object);
        CHECK(static_cast<bool>(handle));
    }
    CHECK(object.releases == 1);
}

TEST_CASE("Handle can own an object type that is never tracked")
{
    FakeRootObject object;
    {
        const Gpu::Handle<FakeRootHandle> handle(&object);
    }
    CHECK(object.releases == 1);
}

TEST_CASE("An empty Handle releases nothing")
{
    const Gpu::Handle<FakeHandle> empty;
    const Gpu::Handle<FakeHandle> null(nullptr);
    CHECK_FALSE(static_cast<bool>(empty));
    CHECK_FALSE(static_cast<bool>(null));
}

TEST_CASE("Moving a Handle transfers ownership")
{
    FakeObject object;
    {
        Gpu::Handle<FakeHandle> source(&object);
        const Gpu::Handle<FakeHandle> target(std::move(source));
        CHECK(target.get() == &object);
        CHECK_FALSE(static_cast<bool>(source));
    }
    CHECK(object.releases == 1);
}

TEST_CASE("Move assignment releases the previous object")
{
    FakeObject first;
    FakeObject second;
    Gpu::Handle<FakeHandle> handle(&first);
    handle = Gpu::Handle<FakeHandle>(&second);
    CHECK(first.releases == 1);
    CHECK(second.releases == 0);
    handle.reset();
    CHECK(second.releases == 1);
    CHECK_FALSE(static_cast<bool>(handle));
}

TEST_CASE("Tracked handles report to Resource::Stats while alive")
{
    Gpu::Resource::Stats stats;
    FakeObject a;
    FakeObject b;
    {
        const Gpu::Handle<FakeHandle> first(&a, stats, 1024);
        Gpu::Handle<FakeHandle> second(&b, stats, 256);
        CHECK(stats.count(Gpu::Resource::Kind::Buffer) == 2);
        CHECK(stats.bytes(Gpu::Resource::Kind::Buffer) == 1280);
        CHECK(stats.totalBytes() == 1280);

        // Moving keeps the object counted once.
        const Gpu::Handle<FakeHandle> moved(std::move(second));
        CHECK(stats.count(Gpu::Resource::Kind::Buffer) == 2);

        CHECK(stats.count(Gpu::Resource::Kind::Texture) == 0);
    }
    CHECK(stats.count(Gpu::Resource::Kind::Buffer) == 0);
    CHECK(stats.totalBytes() == 0);
}

TEST_CASE("A tracked handle around null counts nothing")
{
    Gpu::Resource::Stats stats;
    {
        const Gpu::Handle<FakeHandle> handle(nullptr, stats, 1024);
        CHECK(stats.count(Gpu::Resource::Kind::Buffer) == 0);
        CHECK(stats.totalBytes() == 0);
    }
    CHECK(stats.count(Gpu::Resource::Kind::Buffer) == 0);
}
