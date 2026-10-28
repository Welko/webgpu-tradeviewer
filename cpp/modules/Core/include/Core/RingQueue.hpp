#pragma once

#include <cassert>
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
        assert(!empty());
        T item = std::move(mItems[mOldest]);
        mOldest = (mOldest + 1) % mItems.size();
        --mSize;
        return item;
    }

    /** The item `index` places after the oldest. `index` must be less than size(). */
    T& at(std::size_t index)
    {
        assert(index < size());
        return mItems[(mOldest + index) % mItems.size()];
    }
    const T& at(std::size_t index) const
    {
        assert(index < size());
        return mItems[(mOldest + index) % mItems.size()];
    }

    /** The oldest item. The queue mustn't be empty. */
    T& front() { return at(0); }
    const T& front() const { return at(0); }

    /** The newest item. The queue mustn't be empty. */
    T& back() { return at(size() - 1); }
    const T& back() const { return at(size() - 1); }

    std::size_t size() const { return mSize; }
    std::size_t capacity() const { return mItems.size(); }
    bool empty() const { return mSize == 0; }

private:
    std::vector<T> mItems;
    std::size_t mOldest = 0;  // where the oldest item is in mItems
    std::size_t mSize = 0;
};

} // namespace Core
