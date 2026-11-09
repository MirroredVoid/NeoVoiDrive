#pragma once

/*
    Variable fractional delay line for pitch modulation (T06, DECISIONS §1.3).

    - capacity: 8192 samples (SPEC §5.2: worst case 10 Hz x 1200 cents
      -> A ~= 15.9 ms; x1.5 headroom + interpolation margin -> 4608, aligned
      to 8192 @ 192 kHz);
    - interpolation: 4-tap Catmull-Rom (cubic);
    - the delay depth (in samples, possibly fractional) is passed per sample by
      the caller (PitchModUnit, T07); this class stays a pure delay line.

    Depth range: [0, maxDelaySamples - 1]. The caller supplies the baseline
    delay + modulation (baseline must keep the total >= 1 for interpolation).

    RT contract: zero allocation (fixed std::array), no locks, no exceptions.
*/

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace dsp
{

class ModDelayLine
{
public:
    // SPEC §5.2: capacity aligned to 8192 @ 192 kHz.
    static constexpr int maxDelaySamples = 8192;

    // DECISIONS §1.3: A = (2^(c/1200) - 1) / (2*pi*f) seconds.
    // Peak delay swing (seconds) for a sine modulation of `cents` peak depth
    // at `rateHz`. Defensive: non-positive / non-finite rates clamp to 1 Hz
    // so a corrupted parameter can never produce NaN depths on the audio
    // thread.
    static float centsToSeconds (float cents, float rateHz) noexcept
    {
        const auto safeRate = (rateHz > 0.0f && std::isfinite (rateHz)) ? rateHz : 1.0f;
        jassert (rateHz > 0.0f);
        const auto ratio = std::pow (2.0f, cents / 1200.0f) - 1.0f;
        return ratio / (juce::MathConstants<float>::twoPi * safeRate);
    }

    /** Configures the line. `maxBlockSize` is accepted for interface
        compatibility; the buffer is fixed at compile time. */
    void prepare (double /*sampleRate*/, int /*maxBlockSize*/) noexcept
    {
        reset();
    }

    /** Zeros the buffer and resets the write position. */
    void reset() noexcept
    {
        buffer.fill (0.0f);
        writeIndex = 0;
    }

    /** Writes one input sample and reads the delayed output.
        `delaySamples` in [0, maxDelaySamples - 1], fractional allowed.
        Delays below 1 sample linearly blend between the current and the
        previous sample: depth 0 is bit-transparent (T07 acceptance) and the
        blend meets the interpolation branch continuously at depth 1 (no
        delay step, no click). */
    float process (float x, float delaySamples) noexcept
    {
        jassert (delaySamples >= 0.0f && delaySamples < static_cast<float> (maxDelaySamples));
        // Audit P2-6: >192 kHz can exceed the 8192-sample capacity at the
        // worst-case depth (384 kHz ≈ 13.7 k). Clamp instead of wrapping the
        // read pointer into the wrong history (release builds have no assert).
        delaySamples = juce::jlimit (0.0f, static_cast<float> (getMaxDelaySamples()),
                                     delaySamples);

        buffer[writeIndex] = x;
        writeIndex = (writeIndex == maxDelaySamples - 1) ? 0 : writeIndex + 1;

        return read (delaySamples);
    }

    /** Read-only tap at `delaySamples` (no write, no index advance) — used
        by SidebandEngine's MicroVoice Fog (T36 rev18): several decorrelated
        micro voices share ONE buffer through one write + N reads. */
    float read (float delaySamples) const noexcept
    {
        jassert (delaySamples >= 0.0f && delaySamples < static_cast<float> (maxDelaySamples));
        delaySamples = juce::jlimit (0.0f, static_cast<float> (getMaxDelaySamples()),
                                     delaySamples);

        // Depth < 1: linear blend current<->1-sample-delayed. Continuous with
        // the interpolation branch at depth == 1.
        if (delaySamples < 1.0f)
        {
            const auto last = (writeIndex == 0) ? maxDelaySamples - 1 : writeIndex - 1;
            const auto prev = (last == 0) ? maxDelaySamples - 1 : last - 1;
            return (1.0f - delaySamples) * buffer[last] + delaySamples * buffer[prev];
        }

        // Read position: `delay` samples before the sample just written.
        auto readPos = static_cast<float> (writeIndex - 1) - delaySamples;

        // Normalise into [1, maxDelaySamples + 1) so the 4-tap window
        // [i-1, i+2] stays in bounds (at most two wraps are ever needed).
        while (readPos < 1.0f)
            readPos += static_cast<float> (maxDelaySamples);

        const auto i = static_cast<int> (readPos);
        const auto frac = readPos - static_cast<float> (i);

        // 4-tap Catmull-Rom; indices >= maxDelaySamples wrap by one subtract
        // (i+2 < maxDelaySamples + 3, so a single wrap suffices).
        const auto i0 = i - 1;
        auto i1 = i;
        auto i2 = i + 1;
        auto i3 = i + 2;

        if (i1 >= maxDelaySamples) i1 -= maxDelaySamples;
        if (i2 >= maxDelaySamples) i2 -= maxDelaySamples;
        if (i3 >= maxDelaySamples) i3 -= maxDelaySamples;

        const auto p0 = buffer[i0];
        const auto p1 = buffer[i1];
        const auto p2 = buffer[i2];
        const auto p3 = buffer[i3];

        // Catmull-Rom cubic
        const auto f = frac;
        const auto f2 = f * f;
        const auto f3 = f2 * f;

        return 0.5f * ((2.0f * p1)
                     + (-p0 + p2) * f
                     + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * f2
                     + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * f3);
    }

    /** Largest usable delay depth. */
    static constexpr int getMaxDelaySamples() noexcept
    {
        return maxDelaySamples - 1;
    }

private:
    std::array<float, maxDelaySamples> buffer {};
    int writeIndex = 0;
};

} // namespace dsp
