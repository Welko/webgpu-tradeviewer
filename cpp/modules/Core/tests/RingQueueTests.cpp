#include "Core/RingQueue.hpp"

#include <doctest/doctest.h>

#include <vector>

namespace {

/** The queue's items, oldest first. */
std::vector<int> items(Core::RingQueue<int> ring)
{
    std::vector<int> all;
    while (!ring.empty()) {
        all.push_back(ring.popFront());
    }
    return all;
}

} // namespace

TEST_CASE("A ring queue starts empty")
{
    const Core::RingQueue<int> ring(3);
    CHECK(ring.empty());
    CHECK(ring.size() < ring.capacity());
    CHECK(ring.size() == 0);
    CHECK(ring.capacity() == 3);
}

TEST_CASE("Items come out oldest first")
{
    Core::RingQueue<int> ring(3);
    CHECK_FALSE(ring.pushBack(1));
    CHECK_FALSE(ring.pushBack(2));
    CHECK(ring.front() == 1);
    CHECK(ring.popFront() == 1);
    CHECK(ring.popFront() == 2);
    CHECK(ring.empty());
}

TEST_CASE("Pushing to a full ring queue evicts the oldest item")
{
    Core::RingQueue<int> ring(3);
    ring.pushBack(1);
    ring.pushBack(2);
    ring.pushBack(3);
    CHECK(ring.size() == ring.capacity());

    CHECK(ring.pushBack(4) == 1);
    CHECK(ring.pushBack(5) == 2);
    CHECK(ring.size() == 3);
    CHECK(items(ring) == std::vector<int>{ 3, 4, 5 });
}

TEST_CASE("Items stay in order as the ring queue wraps around")
{
    Core::RingQueue<int> ring(3);
    for (int item = 1; item <= 10; ++item) {
        ring.pushBack(item);
        if (item % 2 == 0) {
            ring.popFront();
        }
    }
    // 10 pushed, 5 popped, 3 evicted: 9 and 10 remain.
    CHECK(items(ring) == std::vector<int>{ 9, 10 });

    CHECK(ring.front() == 9);
    CHECK(ring.back() == 10);
    ring.back() = 100;
    CHECK(ring.popFront() == 9);
    CHECK(ring.front() == 100);
}
