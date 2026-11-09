#pragma once

/*
    ShimmerUnit: gain modulation via S&H random source (T08, DECISIONS §1.3).

        output = input * g(t),   g(t) = 1 - depth * m01(t)

      - m01(t): smoothed S&H noise mapped to [0, 1] (RandomModSource, T05);
        its bandwidth equals the shimmer rate (10-1000 Hz, SPEC §4.3);
      - depth: 0..100 (%) from the snapshot; smoothed with a 20 ms ramp
        (SPEC "平滑" column) so automation never clicks;
      - g(t) in [1 - depth/100, 1]: depth 0 -> g == 1 exactly
        (bit-transparent, T08 acceptance).

    One instance per channel (R6).
    RT contract: zero allocation, no locks, no exceptions.
*/

#include <juce_core/juce_core.h>

#include <cstdint>

#include "RandomModSource.h"
#include "../util/SmoothedValue.h"

namespace dsp
{

class ShimmerUnit
{
public:
    // SPEC §4.3 smoothing column: 20 ms for shimmer depth.
    static constexpr float smoothingMilliseconds = 20.0f;

    /** Per-block parameter bundle (subset of params::ParamSnapshot::Band). */
    struct Params
    {
        float depth = 0.0f;  // % (SPEC shimmer_depth 0-100)
        float rate  = 100.0f; // Hz (SPEC shimmer_rate 10-1000)
    };

    /** Configures sample rate and resets state. */
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        noise.prepare (sampleRate);
        depthSmooth.prepare (sampleRate, smoothingMilliseconds);
        depthSmooth.reset (0.0f);
    }

    /** Applies the per-block parameters. RT safe; depth is smoothed
        internally (20 ms), the rate takes effect immediately (the noise
        signal stays continuous). Depth is clamped to [0, 100] so a corrupted
        parameter can never produce a negative gain. */
    void setParameters (const Params& p) noexcept
    {
        noise.setRate (p.rate);
        depthSmooth.setTarget (juce::jlimit (0.0f, 100.0f, p.depth));
    }

    /** Sets the RNG seed (tests / reproducible setups). RT safe. */
    void setSeed (std::int64_t seed) noexcept
    {
        noise.setSeed (seed);
    }

    /** Advances one sample: gain-modulated output. */
    float process (float x) noexcept
    {
        const auto m01 = (noise.next() + 1.0f) * 0.5f; // [0, 1]
        const auto depth = depthSmooth.getNext() * 0.01f; // % -> 0..1
        const auto g = 1.0f - depth * m01;             // [1-depth, 1]
        lastGain = g;
        return x * g;
    }

    /** Gain applied to the most recent sample (diagnostics / tests). */
    float getLastGain() const noexcept
    {
        return lastGain;
    }

    /** S&H draw count of the internal source (diagnostics / tests). */
    std::uint64_t getHoldCount() const noexcept
    {
        return noise.getHoldCount();
    }

    /** Current smoothed depth in % (diagnostics / tests). */
    float getSmoothedDepth() const noexcept
    {
        return depthSmooth.getCurrentValue();
    }

private:
    RandomModSource noise;
    util::SmoothedValue depthSmooth;
    double sampleRate = 48000.0;
    float lastGain = 1.0f;
};

} // namespace dsp
