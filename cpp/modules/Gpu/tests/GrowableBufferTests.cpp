#include "DeviceTesting.hpp"

#include "Gpu/Context.hpp"
#include "Gpu/GrowableBuffer.hpp"
#include "Gpu/Resource.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>

using DeviceTesting::createContext;

TEST_CASE("A growable buffer refuses to grow past the largest buffer the device takes, and keeps what it has")
{
    auto context = createContext();
    if (!context)
    {
        return;
    }
    // A pretend device whose largest buffer is 20 bytes.
    context->maxBufferBytes = 20;
    Gpu::Resource::Stats stats;
    Gpu::GrowableBuffer buffer(*context, stats, WGPUBufferUsage_Vertex);
    const std::array<std::byte, 16> sixteen{};
    const auto four = std::span(sixteen).first(4);

    const auto refused = buffer.reserve(24);
    CHECK_FALSE(refused);
    if (!refused)
    {
        CHECK(refused.error() == "24 bytes are more than the largest buffer this GPU takes, 20 bytes");
    }
    CHECK(buffer.capacity() == 0);
    CHECK(buffer.append(sixteen));
    CHECK(buffer.capacity() == 16);
    // Half again would be 24; it stops at 20 instead.
    CHECK(buffer.append(four));
    CHECK(buffer.capacity() == 20);
    CHECK_FALSE(buffer.append(four));
    CHECK(buffer.size() == 20);
    CHECK(buffer.reserve(20));
}
