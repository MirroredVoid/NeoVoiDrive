#pragma once

/*
    Sample-and-hold white noise + one-pole smoothing (T05, DECISIONS §1.3).

    Produces a continuous random control signal in [-1, 1]:
      - a new uniform target in [-1, 1] is drawn every 1/rate seconds (S&H);
      - a one-pole low-pass smooths between targets, so the output never clicks.
    `rate` is the noise-equivalent bandwidth: the smoothing time constant is
    tau = 1 / (2*pi*rate), i.e. the -3 dB corner of the one-pole equals `rate`.

    Every instance owns an independent juce::Random (R6). Seeds are derived
    per BAND (MultibandEngine::setSeed) so per-band modulators stay
    uncorrelated, while the LEFT and RIGHT channels share the same seed so a
    mono source stays mono (user requirement).

    RT contract: zero allocation, no locks, no exceptions. setRate() may be
    called from the audio thread (e.g. once per block from ParamSnapshot) — it
    only recomputes coefficients and never disturbs the current output value,
    so no click is introduced.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdint>

namespace dsp
{

class RandomModSource
{
public:
    /** Configures the sample rate and resets the signal state (the RNG keeps
        its seed for continuity). Call from prepareToPlay. */
    void prepare (double newSampleRate) noexcept
    {
        jassert (newSampleRate > 0.0);
        sampleRate = newSampleRate;
        current = 0.0f;
        target = 0.0f;
        samplesUntilHold = 0; // draw a new target on the very first next()
        holdCount = 0;
        recalculate();
    }

    /** Sets the noise bandwidth / S&H update rate in Hz. RT safe, click-free.
        Non-positive / non-finite values are clamped to a safe 0.1 Hz. */
    void setRate (float rateHz) noexcept
    {
        rate = (rateHz > 0.0f && std::isfinite (rateHz)) ? rateHz : 0.1f;
        recalculate();

        // T36 rev26: the samplesUntilHold clamp is REMOVED. It forced a
        // target draw at every block boundary (setRate runs once per block
        // from setParameters) — a 10.7 ms (512-sample) block-rate ripple on
        // the noise output, measured ~17 dB above the dry baseline at
        // 93.75 Hz on a held tone: the fixed-position texture the user
        // heard across every revision. The rate transition now settles
        // within one OLD period (≤ 1/oldRate — inaudible at these
        // bandwidths) instead of snapping at the block edge.
    }

    /** Sets the RNG seed (tests / reproducible setups). RT safe. */
    void setSeed (std::int64_t seed) noexcept
    {
        random.setSeed (seed);
    }

    /** Selects continuous vs sample-and-hold mode. Continuous (T39 rev38):
        a NEW random target every sample, still through the same one-pole —
        i.e. band-limited white noise with no S&H staircase. The staircase
        is what read as coarse "grain" on top of the fog; continuous noise
        is a smooth, uniform haze. */
    void setContinuous (bool on) noexcept
    {
        continuous = on;
        samplesUntilHold = 1;      // draw immediately
        recalculate();             // continuous mode needs the gain compensation
    }

    /** Advances one sample; returns the smoothed random value in [-1, 1]. */
    float next() noexcept
    {
        if (continuous || --samplesUntilHold <= 0)
        {
            target = random.nextFloat() * 2.0f - 1.0f;
            samplesUntilHold = continuous ? 1 : samplesPerHold;
            ++holdCount;
        }

        current += static_cast<float> (alpha) * (target - current);
        // T39 rev38: in continuous mode the one-pole low-passes WHITE noise,
        // which attenuates the rms by sqrt(alpha/(2-alpha)) — compensate so
        // the fog density matches the S&H version (a staircase sits at unity
        // DC gain, so it needs no compensation).
        if (continuous)
            return current * noiseGain;
        return current;
    }

    /** Number of S&H draws performed so far (diagnostics / tests). */
    uint64_t getHoldCount() const noexcept
    {
        return holdCount;
    }

private:
    void recalculate() noexcept
    {
        samplesPerHold = juce::jmax (1, static_cast<int> (std::lround (sampleRate / static_cast<double> (rate))));

        // One-pole time constant tau = 1/(2*pi*rate)  ->  -3 dB bandwidth == rate
        // (DECISIONS §1.3: "rate 即噪声等效带宽").
        const auto tau = 1.0 / (2.0 * juce::MathConstants<double>::pi * static_cast<double> (rate));
        alpha = 1.0 - std::exp (-1.0 / (sampleRate * tau));

        // Continuous-mode white noise through a one-pole: output rms = input
        // rms * sqrt(alpha/(2-alpha)). Fold that into noiseGain (=1 in S&H,
        // where the smoothed value reaches unity DC gain).
        noiseGain = static_cast<float> (std::sqrt ((2.0 - alpha) / juce::jmax (alpha, 1e-6)));
    }

    bool continuous = false;
    float noiseGain = 1.0f;

    double sampleRate = 48000.0;
    float rate = 25.0f;
    int samplesPerHold = 1;
    int samplesUntilHold = 0;
    float target = 0.0f;
    float current = 0.0f;
    double alpha = 1.0;
    uint64_t holdCount = 0;
    juce::Random random; // independent per instance (R6)
};

} // namespace dsp
