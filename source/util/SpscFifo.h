#pragma once

/*
    Single-producer / single-consumer lock-free FIFO (T04).

    Thin wrapper around juce::AbstractFifo with a compile-time capacity
    (std::array) so the steady state performs zero heap allocation.

    Threading contract (ARCHITECTURE §6):
      - exactly ONE writer thread calls push();
      - exactly ONE reader thread calls pop();
      - push() never blocks: when the buffer is full the item is dropped and
        false is returned ("写满即丢") — the audio thread must never stall.
*/

#include <juce_core/juce_core.h>

#include <array>

namespace util
{

template <typename T, int Capacity>
class SpscFifo
{
public:
    static_assert (Capacity > 0, "SpscFifo capacity must be positive");

    /** Default-constructs; storage is a member array, no heap allocation. */
    SpscFifo() = default;

    /** Appends one item. Returns false (and drops the item) when full.
        The buffer holds exactly `capacity()` items; the drop never blocks. */
    bool push (const T& item) noexcept
    {
        const juce::AbstractFifo::ScopedWrite sw (fifo, 1);

        if (sw.blockSize1 == 0) // no free space -> drop, never block
            return false;

        storage[static_cast<size_t> (sw.startIndex1)] = item;
        return true;
    }

    /** Removes one item into `item`. Returns false when empty. */
    bool pop (T& item) noexcept
    {
        const juce::AbstractFifo::ScopedRead sr (fifo, 1);

        if (sr.blockSize1 == 0)
            return false;

        item = storage[static_cast<size_t> (sr.startIndex1)];
        return true;
    }

    /** Number of items currently available to read. */
    int available() const noexcept   { return fifo.getNumReady(); }

    /** Number of free slots currently available to write. */
    int freeSpace() const noexcept   { return fifo.getFreeSpace(); }

    /** Empties the buffer (must not be called concurrently with push/pop). */
    void reset() noexcept            { fifo.reset(); }

    /** Total capacity in items. */
    static constexpr int capacity() noexcept { return Capacity; }

private:
    // AbstractFifo manages bufferSize-1 items, so the internal buffer size is
    // Capacity+1 and the backing storage must match it exactly — the write
    // index can reach Capacity when the buffer has been full and partially
    // drained (a wrap-around scenario). `capacity()` items are usable.
    juce::AbstractFifo fifo { Capacity + 1 };
    std::array<T, static_cast<size_t> (Capacity) + 1> storage {};
};

} // namespace util
