#pragma once

/*
    4-waveform pitch-modulation LFO (T06, DECISIONS §1.3).

    Waveforms (SPEC §4.3 `{n}_vib_wave`):
      sine     sin(2*pi*f*t)                          -> [-1, 1]
      square   +-1 at 50% duty                        -> {+1, -1}
      fullWave |sin(2*pi*f*t)| (full-wave rectified)  -> [0, 1] (2x rate)
      noise    S&H + smoothed random source (T05)     -> [-1, 1]

    Every output passes through a slew limiter (R5): a full +-1 -> -+1 swing
    takes `slewMs` (default 1 ms). This removes the delay-line step that would
    otherwise click on square edges, while being far above the maximum slope of
    a legal sine (<= 300 Hz), so sine modulation is never distorted.

    RT contract: zero allocation, no locks, no exceptions. All setters are
    audio-thread safe and click-free (phase / state are never reset).
*/

#include <juce_core/juce_core.h>

#include <cmath>

#include "RandomModSource.h"

namespace dsp
{

class ModLFO
{
public:
    enum class Waveform
    {
        sine,
        square,
        fullWave,
        noise
    };

    static constexpr float defaultSlewMs = 1.0f; // R5: 1-3 ms

    /** Configures the sample rate and resets state. Call from prepareToPlay. */
    void prepare (double newSampleRate) noexcept
    {
        jassert (newSampleRate > 0.0);
        sampleRate = newSampleRate;
        phase = 0.0f;
        currentOut = 0.0f;
        noiseSource.prepare (sampleRate);
        recalculate();
    }

    /** Selects the waveform. RT safe. */
    void setWaveform (Waveform w) noexcept
    {
        waveform = w;
    }

    /** Sets the modulation rate in Hz (noise waveform: noise bandwidth). */
    void setRate (float rateHz) noexcept
    {
        jassert (rateHz > 0.0f);
        rate = rateHz;
        recalculate();
    }

    /** Sets the slew duration for a full-scale swing in ms (R5). */
    void setSlewMs (float ms) noexcept
    {
        slewMs = ms;
        recalculate();
    }

    /** Sets the RNG seed of the internal noise source (tests / reproducible
        setups, R6: distinct instances must stay uncorrelated). RT safe. */
    void setSeed (std::int64_t seed) noexcept
    {
        noiseSource.setSeed (seed);
    }

    /** Advances one sample; returns the slew-limited waveform value. */
    float next() noexcept
    {
        float target;

        switch (waveform)
        {
            case Waveform::sine:
                target = std::sin (juce::MathConstants<float>::twoPi * phase);
                break;

            case Waveform::square:
                target = phase < 0.5f ? 1.0f : -1.0f;
                break;

            case Waveform::fullWave:
                target = std::abs (std::sin (juce::MathConstants<float>::twoPi * phase));
                break;

            case Waveform::noise:
            default:
                target = noiseSource.next();
                break;
        }

        // Slew limiter: no sample-to-sample change larger than maxStep.
        const auto delta = target - currentOut;
        currentOut += juce::jlimit (-maxStep, maxStep, delta);

        // Phase advance (noise waveform is not phase-driven).
        phase += phaseIncrement;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return currentOut;
    }

private:
    void recalculate() noexcept
    {
        phaseIncrement = static_cast<float> (rate / sampleRate);
        // Full-scale (2.0) swing completed within slewMs.
        maxStep = static_cast<float> (2.0 / (slewMs * 0.001 * sampleRate));
        noiseSource.setRate (rate);
    }

    double sampleRate = 48000.0;
    Waveform waveform = Waveform::sine;
    float rate = 1.0f;
    float slewMs = defaultSlewMs;
    float phase = 0.0f;
    float phaseIncrement = 0.0f;
    float currentOut = 0.0f;
    float maxStep = 1.0f;
    RandomModSource noiseSource; // independent RNG (R6)
};

} // namespace dsp
