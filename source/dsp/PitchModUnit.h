#pragma once

/*
    Pitch modulation unit: jitter + vibration merged onto ONE delay line
    (T07, ARCHITECTURE §2, DECISIONS §1.3).

      - jitter:     RandomModSource (T05), depth in cents, rate = noise BW;
      - vibration:  ModLFO (T06), 4 waveforms, depth in cents;
      - both modulation signals are summed in the delay-depth domain and drive
        a single ModDelayLine (T06).

    Depth model (DECISIONS §1.3):
        A = (2^(c/1200) - 1) / (2*pi*f)  seconds  (peak swing)
        depth(t) = A_j * (1 + m_j(t)) + A_v * (1 + m_v(t))   samples

    With A == 0 (all depths zero) the depth is 0 and the delay line is
    bit-transparent. The peak amplitude A is smoothed with a 20 ms ramp
    (SPEC §4.3 "平滑" column) so parameter automation never clicks; the
    modulation signals themselves are inherently continuous.

    One instance per channel (R6: independent RNGs per instance).

    RT contract: zero allocation, no locks, no exceptions.
*/

#include <juce_core/juce_core.h>

#include <cstdint>

#include "ModDelayLine.h"
#include "ModLFO.h"
#include "RandomModSource.h"
#include "../util/SmoothedValue.h"

namespace dsp
{

class PitchModUnit
{
public:
    // SPEC §4.3 smoothing column: 20 ms for jitter/vib depths (and rates).
    static constexpr float smoothingMilliseconds = 20.0f;

    /** Per-block parameter bundle (subset of params::ParamSnapshot::Band). */
    struct Params
    {
        float jitterDepth = 0.0f;  // cents
        float jitterRate  = 100.0f; // Hz (UI default, ParameterLayout)
        float vibDepth    = 0.0f;  // cents
        float vibRate     = 10.0f; // Hz
        ModLFO::Waveform vibWave = ModLFO::Waveform::sine;
    };

    /** Configures sample rate, smoothing and the delay line. */
    void prepare (double newSampleRate, int maxBlockSize) noexcept
    {
        sampleRate = newSampleRate;
        jitter.prepare (sampleRate);
        vibration.prepare (sampleRate);
        delayLine.prepare (sampleRate, maxBlockSize);

        jitterAmp.prepare (sampleRate, smoothingMilliseconds);
        vibAmp.prepare (sampleRate, smoothingMilliseconds);
        jitterAmp.reset (0.0f);
        vibAmp.reset (0.0f);
    }

    /** Applies the per-block parameters. RT safe; the amplitude targets are
        smoothed internally (20 ms), rates take effect immediately (the
        modulation signals stay continuous). Depth/rate are clamped so a
        corrupted parameter can never produce a huge delay or a zero/negative
        rate (O3, review). */
    void setParameters (const Params& p) noexcept
    {
        const auto depth = [&] (float d) { return juce::jlimit (0.0f, 1200.0f, d); };
        // Rate ceiling 1000 Hz: the UI jitter/shimmer rate range was raised
        // to 100-1000 Hz (user spec); a 200 Hz clamp silently nullified it.
        const auto rate  = [&] (float r) { return juce::jlimit (0.1f, 1000.0f, r); };

        jitter.setRate (rate (p.jitterRate));
        vibration.setRate (rate (p.vibRate));
        vibration.setWaveform (p.vibWave);

        jitterAmp.setTarget (ModDelayLine::centsToSeconds (depth (p.jitterDepth), rate (p.jitterRate))
                             * static_cast<float> (sampleRate));
        vibAmp.setTarget (ModDelayLine::centsToSeconds (depth (p.vibDepth), rate (p.vibRate))
                          * static_cast<float> (sampleRate));
    }

    /** Sets the RNG seeds of both internal sources (tests / reproducibility). */
    void setSeed (std::int64_t seed) noexcept
    {
        jitter.setSeed (seed);
        vibration.setSeed (static_cast<std::int64_t> (static_cast<std::uint64_t> (seed)
                                                      ^ 0x9E3779B97F4A7C15ull)); // distinct stream
    }

    /** Advances one sample. */
    float process (float x) noexcept
    {
        const auto mj = jitter.next();
        const auto mv = vibration.next();

        // Smoothed peak amplitudes (samples).
        const auto aj = jitterAmp.getNext();
        const auto av = vibAmp.getNext();

        // Depth in [0, 2*(aj+av)]; 0 when all depths are zero (transparent).
        const auto depth = aj * (1.0f + mj) + av * (1.0f + mv);
        lastDepth = depth;

        return delayLine.process (x, depth);
    }

    /** Clears the delay line's audio history (structural routing changes).
        No reallocation; the modulation sources keep their phase. */
    void resetState() noexcept
    {
        delayLine.reset();
    }

    /** Current smoothed jitter peak amplitude in samples (diagnostics). */
    float getJitterAmpSamples() const noexcept
    {
        return jitterAmp.getCurrentValue();
    }

    /** Current smoothed vibration peak amplitude in samples (diagnostics). */
    float getVibAmpSamples() const noexcept
    {
        return vibAmp.getCurrentValue();
    }

    /** Delay depth used for the most recent sample (diagnostics / tests). */
    float getLastDepth() const noexcept
    {
        return lastDepth;
    }

private:
    RandomModSource jitter;
    ModLFO vibration;
    ModDelayLine delayLine;
    util::SmoothedValue jitterAmp;
    util::SmoothedValue vibAmp;
    double sampleRate = 48000.0;
    float lastDepth = 0.0f; // diagnostics
};

} // namespace dsp
