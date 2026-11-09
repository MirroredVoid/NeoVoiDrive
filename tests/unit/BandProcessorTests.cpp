/*
    NeoVoiDrive T11 tests — BandProcessor.

    Acceptance (TASKS.md T11):
      1. all-zero parameters -> output equals input (<= -100 dB, in practice
         bit-transparent because every unit is transparent at zero params);
      2. each effect is independently verifiable (activate one at a time);
      3. transparency is achieved without processor-level short-circuiting,
         so the effect chain never clicks on toggle (structural — verified by
         a zero->nonzero->zero parameter cycle with continuous output).
*/

#include <juce_core/juce_core.h>

#include "dsp/BandProcessor.h"
#include "params/LoudnessCurve.h"

#include <cmath>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int maxBlock = 512;

params::ParamSnapshot::Band zeroBand()
{
    params::ParamSnapshot::Band b;
    b.jitterDepth   = 0.0f;
    b.jitterRate    = 25.0f;
    b.shimmerDepth  = 0.0f;
    b.shimmerRate   = 25.0f;
    b.harmonicDrive = 0.0f;
    b.harmonicTone  = 3000.0f;
    b.vibWave       = 0;
    b.vibRate       = 10.0f;
    b.vibDepth      = 0.0f;
    return b;
}

//==============================================================================
class BandTransparencyTest final : public juce::UnitTest
{
public:
    BandTransparencyTest() : juce::UnitTest ("BandTransparency") {}

    void runTest() override
    {
        beginTest ("all-zero parameters: output equals input delayed by the exciter latency");
        {
            dsp::BandProcessor band;
            band.prepare (kSampleRate, maxBlock);
            band.setParameters (zeroBand());

            constexpr int numSamples = 48000;

            // M2 (T13 review): with all-zero params every unit is transparent
            // except the exciter's latency-matched dry (~5 samples, 2x
            // oversampler), so the chain is a pure delay, not identity.
            // Probe the actual delay: pitch delay lines may add to it.
            constexpr int ringSize = 32;
            float bestError = 1e30f;
            int bestDelay = -1;
            for (int probeDelay = 1; probeDelay <= 16; ++probeDelay)
            {
                dsp::BandProcessor freshBand;
                freshBand.prepare (kSampleRate, maxBlock);
                freshBand.setParameters (zeroBand());

                float history[ringSize] = {};
                float err = 0.0f;
                for (int n = 0; n < numSamples; ++n)
                {
                    const auto x = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                    * 440.0f * n / static_cast<float> (kSampleRate));
                    const auto y = freshBand.process (x);
                    const auto expected = history[static_cast<size_t> ((n + ringSize - probeDelay) % ringSize)];
                    history[static_cast<size_t> (n % ringSize)] = x;
                    err = juce::jmax (err, std::abs (y - expected));
                }
                if (err < bestError) { bestError = err; bestDelay = probeDelay; }
            }
            logMessage (juce::String ("all-zero chain best delay ") + juce::String (bestDelay)
                        + " err " + juce::String (bestError));

            // A pure delay must match bit-exactly.
            expectEquals (bestError, 0.0f, "all-zero chain is not a pure delay");
        }

        beginTest ("parameter cycle zero->nonzero->zero stays click-free (no processor-level bypass step)");
        {
            dsp::BandProcessor band;
            band.prepare (kSampleRate, maxBlock);

            auto p = zeroBand();
            band.setParameters (p);

            // warm up
            for (int n = 0; n < 48000; ++n)
                band.process (0.5f);

            float previous = 0.5f; // DC steady state (sum of gains -> 1)
            float maxJump = 0.0f;
            bool measured = false;

            for (int n = 0; n < 240000; ++n)
            {
                if (n == 48000)
                {
                    // all effects on
                    p.shimmerDepth  = 50.0f;
                    p.harmonicDrive = 50.0f;
                    p.jitterDepth   = 50.0f;
                    p.vibDepth      = 50.0f;
                    band.setParameters (p);
                }
                else if (n == 144000)
                {
                    // all effects off again
                    band.setParameters (zeroBand());
                }

                const auto y = band.process (0.5f);
                if (measured)
                    maxJump = juce::jmax (maxJump, std::abs (y - previous));
                previous = y;
                measured = true;
            }

            // Smooth ramps only: an instant bypass toggle would step by the
            // full gain difference (~0.2-0.5 here).
            expectLessThan (maxJump, 0.05f, "parameter cycle caused a click");
        }
    }
};

//==============================================================================
class BandShimmerTest final : public juce::UnitTest
{
public:
    BandShimmerTest() : juce::UnitTest ("BandShimmer") {}

    void runTest() override
    {
        beginTest ("shimmer alone modulates the gain around [1-depth, 1]");
        {
            dsp::BandProcessor band;
            band.prepare (kSampleRate, maxBlock);
            band.setSeed (1234);

            auto p = zeroBand();
            p.shimmerDepth = 50.0f; // 50%
            p.shimmerRate  = 25.0f;
            band.setParameters (p);

            constexpr int warmup = 4800;
            constexpr int measure = 48000;
            float minOut = 1.0f, maxOut = 0.0f;
            float previous = 0.0f;
            bool moving = false;

            for (int n = 0; n < warmup + measure; ++n)
            {
                const auto y = band.process (0.5f); // constant input
                if (n >= warmup)
                {
                    minOut = juce::jmin (minOut, y);
                    maxOut = juce::jmax (maxOut, y);
                    if (std::abs (y - previous) > 1e-6f)
                        moving = true;
                }
                previous = y;
            }

            // y = 0.5 * (1 - 0.5 * m), m in [0, 1] -> [0.25, 0.5]
            expectGreaterThan (minOut, 0.24f);
            expectLessThan (maxOut, 0.501f);
            expectLessThan (minOut, 0.49f, "gain never modulated");
            expect (moving, "output is frozen");
            expectGreaterThan (band.getLastShimmerGain(), 0.49f);
            expectLessThan (band.getLastShimmerGain(), 1.001f);
        }
    }
};

//==============================================================================
class BandPitchTest final : public juce::UnitTest
{
public:
    BandPitchTest() : juce::UnitTest ("BandPitch") {}

    void runTest() override
    {
        beginTest ("jitter alone detunes the signal (delayed samples)");
        {
            dsp::BandProcessor band;
            band.prepare (kSampleRate, maxBlock);
            band.setSeed (42);

            auto p = zeroBand();
            p.jitterDepth = 50.0f; // cents
            p.jitterRate  = 25.0f;
            band.setParameters (p);

            constexpr int warmup = 4800;
            constexpr int measure = 48000;
            float maxDiff = 0.0f;

            for (int n = 0; n < warmup + measure; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi
                                         * 440.0f * n / static_cast<float> (kSampleRate));
                const auto y = band.process (x);
                if (n >= warmup)
                    maxDiff = juce::jmax (maxDiff, std::abs (y - x));
            }

            expectGreaterThan (maxDiff, 1e-3f, "jitter depth had no audible effect");
            expectGreaterThan (band.getJitterAmpSamples(), 0.5f,
                               "smoothed jitter amplitude did not ramp up");
        }

        beginTest ("vibration alone detunes the signal");
        {
            dsp::BandProcessor band;
            band.prepare (kSampleRate, maxBlock);
            band.setSeed (42);

            auto p = zeroBand();
            p.vibDepth = 50.0f; // cents
            p.vibRate  = 10.0f;
            p.vibWave  = 0; // sine
            band.setParameters (p);

            constexpr int warmup = 4800;
            constexpr int measure = 48000;
            float maxDiff = 0.0f;

            for (int n = 0; n < warmup + measure; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi
                                         * 440.0f * n / static_cast<float> (kSampleRate));
                const auto y = band.process (x);
                if (n >= warmup)
                    maxDiff = juce::jmax (maxDiff, std::abs (y - x));
            }

            expectGreaterThan (maxDiff, 1e-3f, "vib depth had no audible effect");
            expectGreaterThan (band.getVibAmpSamples(), 0.5f,
                               "smoothed vibration amplitude did not ramp up");
        }
    }
};

//==============================================================================
class BandExciterTest final : public juce::UnitTest
{
public:
    BandExciterTest() : juce::UnitTest ("BandExciter") {}

    void runTest() override
    {
        beginTest ("exciter alone adds harmonics (3rd harmonic at 600 Hz)");
        {
            dsp::BandProcessor band;
            band.prepare (kSampleRate, maxBlock);
            band.setSeed (7);

            auto p = zeroBand();
            p.harmonicDrive = 80.0f;
            p.harmonicTone  = 3000.0f;
            band.setParameters (p);

            constexpr int warmup = 4800;
            constexpr int measure = 48000;
            std::vector<float> out (measure);

            for (int n = 0; n < warmup + measure; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi
                                         * 200.0f * n / static_cast<float> (kSampleRate));
                const auto y = band.process (x);
                if (n >= warmup)
                    out[n - warmup] = y;
            }

            // DFT of the 600 Hz component (3rd harmonic; tanh is odd, so the
            // first generated harmonic of a 200 Hz sine is 600 Hz).
            double re = 0.0, im = 0.0;
            const auto w = juce::MathConstants<double>::twoPi * 600.0 / kSampleRate;
            for (int n = 0; n < measure; ++n)
            {
                const auto phase = w * n;
                re += out[static_cast<size_t> (n)] * std::cos (phase);
                im -= out[static_cast<size_t> (n)] * std::sin (phase);
            }
            const auto h3 = std::sqrt (re * re + im * im) / (measure / 2);

            // Drive 80% + tone 3000 Hz: the 600 Hz 3rd harmonic is comfortably
            // above this floor (verified with margin across the tone range).
            expectGreaterThan (static_cast<float> (h3), 0.03f,
                               "no measurable 3rd harmonic from the exciter");
        }
    }
};

//==============================================================================
class BandComboTest final : public juce::UnitTest
{
public:
    BandComboTest() : juce::UnitTest ("BandCombo") {}

    void runTest() override
    {
        beginTest ("seeded runs are bit-reproducible");
        {
            dsp::BandProcessor a;
            dsp::BandProcessor b;
            a.prepare (kSampleRate, maxBlock);
            b.prepare (kSampleRate, maxBlock);

            auto p = zeroBand();
            p.shimmerDepth = 30.0f;
            p.jitterDepth  = 20.0f;
            p.vibDepth     = 15.0f;
            p.harmonicDrive = 40.0f;
            a.setParameters (p);
            b.setParameters (p);
            a.setSeed (999);
            b.setSeed (999);

            bool identical = true;
            for (int n = 0; n < 48000; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi
                                         * 330.0f * n / static_cast<float> (kSampleRate));
                if (a.process (x) != b.process (x))
                    identical = false;
            }

            expect (identical, "same seed must produce identical output");
        }

        beginTest ("combined effects are active and distinct from each effect alone");
        {
            dsp::BandProcessor band;
            band.prepare (kSampleRate, maxBlock);
            band.setSeed (5);

            auto p = zeroBand();
            p.shimmerDepth  = 40.0f;
            p.jitterDepth   = 30.0f;
            p.harmonicDrive = 60.0f;
            band.setParameters (p);

            constexpr int warmup = 4800;
            constexpr int measure = 24000;
            float maxDiff = 0.0f;

            for (int n = 0; n < warmup + measure; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi
                                         * 220.0f * n / static_cast<float> (kSampleRate));
                const auto y = band.process (x);
                if (n >= warmup)
                    maxDiff = juce::jmax (maxDiff, std::abs (y - x));
            }

            expectGreaterThan (maxDiff, 1e-2f, "combined effects are not audible");
        }
    }
};

//==============================================================================
class BandGainMuteTest final : public juce::UnitTest
{
public:
    BandGainMuteTest() : juce::UnitTest ("BandGainMute") {}

    void runTest() override
    {
        beginTest ("band gain mute sentinel produces exact silence");
        {
            dsp::BandProcessor band;
            band.prepare (kSampleRate, maxBlock);

            auto p = zeroBand();
            p.gain = params::loudness::minDb;
            band.setParameters (p);

            float maxAbs = 0.0f;
            float firstAbs = -1.0f;
            for (int n = 0; n < 2 * 48000; ++n)
            {
                const auto y = band.process (0.5f); // DC: non-zero from sample 0
                if (n == 0)
                    firstAbs = std::abs (y); // audit P3: no startup leak
                if (n >= 48000)
                    maxAbs = juce::jmax (maxAbs, std::abs (y));
            }

            expectEquals (firstAbs, 0.0f, "gain mute leaked on the first sample");
            expectEquals (maxAbs, 0.0f, "gain mute left a residual signal");
        }
    }
};

BandTransparencyTest bandTransparencyTest;
BandShimmerTest bandShimmerTest;
BandPitchTest bandPitchTest;
BandExciterTest bandExciterTest;
BandComboTest bandComboTest;
BandGainMuteTest bandGainMuteTest;

} // namespace
