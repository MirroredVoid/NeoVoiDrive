#pragma once

/*
    SpectrumSource (T14): input-spectrum bridge from the audio thread to the
    UI thread (ARCHITECTURE §3).

      audio thread:  pushSamples() — decimates the input to ~48 kHz-equivalent
                     and appends to an SPSC FIFO. Never blocks, never allocates:
                     when the buffer is full the sample is dropped ("写满即丢")
                     and the audio thread is unaffected (UI closed / paused).
      UI thread:     readBlock() — pops up to numSamples; readNewestBlock()
                     keeps only the newest FFT window (production path).

    Decimation ratio adapts to the sample rate so the FIFO always covers about
    341 ms of *48 kHz-equivalent* audio (16384 / 48000; SPEC §5.2):
      44.1/48 kHz -> 1 (full rate), 96 kHz -> 2, 192 kHz -> 4.
    No anti-alias filter: this is a display channel, not an audio path.

    Threading contract (ARCHITECTURE §6): exactly one writer (audio thread),
    exactly one reader (UI thread). Zero allocation, no locks — RT safe.
*/

#include "../util/SpscFifo.h"

#include <algorithm>
#include <cmath>

namespace ui
{

class SpectrumSource
{
public:
    // SPEC §5.2: 16384 decimated samples ≈ 341 ms @48 kHz-equivalent.
    static constexpr int fifoCapacity = 16384;
    // FFT block size the UI consumes per refresh (T14: 2048 points).
    static constexpr int fftBlockSize = 2048;

    /** Configures the decimation ratio from the sample rate. Call from
        prepareToPlay (not RT). */
    void prepare (double sampleRate) noexcept
    {
        decimationRatio = std::max (1, static_cast<int> (std::lround (sampleRate / 48000.0)));
        streamSampleRate = sampleRate / decimationRatio; // actual decimated rate (O1)
        decimationCounter = 0;
        fifo.reset();
    }

    /** Actual sample rate of the decimated stream (O1 review: 44.1 kHz
        hosts decimate by 1, so the UI must scale bins by this, not 48k). */
    double getEffectiveSampleRate() const noexcept { return streamSampleRate; }

    //==========================================================================
    // ---- writer side (audio thread) ----
    //==========================================================================

    /** Appends decimated samples. Never blocks: when the FIFO is full the
        new samples are dropped. RT safe (no allocation, no locks).
        Decimation starts at the first sample of each stream (prepare/reset). */
    void pushSamples (const float* input, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (decimationCounter == 0)
                fifo.push (input[i]); // drops when full — never blocks
            if (++decimationCounter >= decimationRatio)
                decimationCounter = 0;
        }
    }

    /** Stereo variant (O4 review): mono-downmixes so wide material isn't
        biased to one channel. RT safe, same drop-when-full contract. */
    void pushSamples (const float* left, const float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (decimationCounter == 0)
                fifo.push (0.5f * (left[i] + right[i])); // drops when full — never blocks
            if (++decimationCounter >= decimationRatio)
                decimationCounter = 0;
        }
    }

    //==========================================================================
    // ---- reader side (UI thread) ----
    //==========================================================================

    /** Pops up to `numSamples` into `dest` (in FIFO order) and returns the
        number actually read. Not RT (UI thread). */
    int readBlock (float* dest, int numSamples) noexcept
    {
        int n = 0;
        while (n < numSamples && fifo.pop (dest[static_cast<size_t> (n)]))
            ++n;
        return n;
    }

    /** Discards the oldest `numSamples` (UI: keep the window at the newest
        data before reading a FFT block). Not RT (UI thread). */
    void skip (int numSamples) noexcept
    {
        float dummy = 0.0f;
        for (int i = 0; i < numSamples && fifo.pop (dummy); ++i) {}
    }

    /** UI: discards the stale head and reads the newest `blockSize` samples.
        Returns 0 while fewer than `blockSize` samples are buffered. This is
        the production reader (audit P2-1): without the skip, a FIFO that is
        not drained (display hidden / other mode selected) replays old data
        for as long as it takes to catch up. */
    int readNewestBlock (float* dest, int blockSize) noexcept
    {
        const auto availableSamples = fifo.available();
        if (availableSamples < blockSize)
            return 0;

        skip (availableSamples - blockSize);
        return readBlock (dest, blockSize);
    }

    /** UI: drops everything buffered (used for the spectrum that is not
        currently displayed, so it never accumulates a stale backlog). */
    void discardAll() noexcept
    {
        skip (fifo.available());
    }

    /** Number of decimated samples currently available. */
    int available() const noexcept { return fifo.available(); }

    /** True when at least one full FFT block is available. */
    bool hasFullBlock() const noexcept { return fifo.available() >= fftBlockSize; }

    /** Empties the FIFO (not concurrent with push/pop). */
    void reset() noexcept
    {
        fifo.reset();
        decimationCounter = 0;
    }

private:
    util::SpscFifo<float, fifoCapacity> fifo;
    int decimationRatio = 1;
    int decimationCounter = 0;
    double streamSampleRate = 48000.0;
};

} // namespace ui
