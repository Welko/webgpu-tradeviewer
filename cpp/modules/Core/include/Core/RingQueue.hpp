#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace Core {

/**
 * A first-in, first-out queue of at most `capacity` items, stored
 * contiguously and allocated once. When full, pushBack() makes room by
 * removing the oldest item. T must be default-constructible.
 */
template<typename T>
class RingQueue
{
public:
    explicit RingQueue(std::size_t capacity)
        : mItems(capacity)
    {}

    /** Adds `item` as the newest. If the queue was full, removes the oldest first and returns it. */
    std::optional<T> pushBack(T item)
    {
        std::optional<T> evicted;
        if (size() == capacity()) {
            evicted = popFront();
        }
        mItems[(mOldest + mSize) % mItems.size()] = std::move(item);
        ++mSize;
        return evicted;
    }

    /** Removes the oldest item and returns it. The queue mustn't be empty. */
    T popFront()
    {
        T item = std::move(mItems[mOldest]);
        mOldest = (mOldest + 1) % mItems.size();
        --mSize;
        return item;
    }

    /** The oldest item. The queue mustn't be empty. */
    T& front() { return mItems[mOldest]; }
    const T& front() const { return mItems[mOldest]; }

    /** The newest item. The queue mustn't be empty. */
    T& back() { return mItems[newest()]; }
    const T& back() const { return mItems[newest()]; }

    std::size_t size() const { return mSize; }
    std::size_t capacity() const { return mItems.size(); }
    bool empty() const { return mSize == 0; }

private:
    std::size_t newest() const { return (mOldest + mSize - 1) % mItems.size(); }

    std::vector<T> mItems;
    std::size_t mOldest = 0;  // where the oldest item is in mItems
    std::size_t mSize = 0;
};

} // namespace Core
