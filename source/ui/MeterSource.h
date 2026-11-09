#pragma once

/*
    MeterSource (T14): output-peak bridge from the audio thread to the UI
    thread (ARCHITECTURE §3 / MeterComponent).

      audio thread:  pushPeaks() — called once per block end from the
                     MultibandEngine meter tap (T13). Appends {left, right}
                     peaks to an SPSC FIFO. Never blocks: when the buffer is
                     full the item is dropped ("写满即丢").
      UI thread:     readLatest() — returns the most recent peaks (drains
                     older history; the meter shows the current value).
                     readOldest() — for tests / history inspection.

    The writer signature matches dsp::MultibandEngine::MeterTap exactly, so
    PluginProcessor can bridge them with a plain function pointer (T14).

    Threading contract: exactly one writer (audio thread), exactly one reader
    (UI thread). Zero allocation, no locks — RT safe.
*/

#include "../util/SpscFifo.h"

namespace ui
{

class MeterSource
{
public:
    // Peak history in blocks (~34 s at 30 Hz refresh; the UI drains it).
    static constexpr int meterCapacity = 1024;

    /** One block-end peak pair. */
    struct Peaks
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    //==========================================================================
    // ---- writer side (audio thread) ----
    //==========================================================================

    /** Appends one block's peaks. Never blocks: drops when full. RT safe. */
    void pushPeaks (float leftPeak, float rightPeak) noexcept
    {
        fifo.push (Peaks { leftPeak, rightPeak });
    }

    //==========================================================================
    // ---- reader side (UI thread) ----
    //==========================================================================

    /** Returns the most recent peaks (draining older history). Returns false
        when nothing has been written yet. Not RT (UI thread). */
    bool readLatest (float& left, float& right) noexcept
    {
        Peaks p;
        bool got = false;
        while (fifo.pop (p))
            got = true; // drain to the newest item
        if (got)
        {
            left = p.left;
            right = p.right;
        }
        return got;
    }

    /** Pops the oldest available peaks. Returns false when empty. */
    bool readOldest (float& left, float& right) noexcept
    {
        Peaks p;
        if (! fifo.pop (p))
            return false;
        left = p.left;
        right = p.right;
        return true;
    }

    /** Number of block-peak items pending. */
    int available() const noexcept { return fifo.available(); }

    /** Empties the FIFO (not concurrent with push/pop). */
    void reset() noexcept { fifo.reset(); }

private:
    util::SpscFifo<Peaks, meterCapacity> fifo;
};

} // namespace ui
