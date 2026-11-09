/*
    NeoVoiDrive T07 tests — PitchModUnit (jitter + vibration merge).

    Acceptance (TASKS.md T07):
      1. jitter_depth=0 && vib_depth=0 -> output bit-transparent
         (<= -100 dBFS difference);
      2. vib-only sine swing meets the set cents (same method as T06, +-5%);
      3. jitter-only short-time pitch perturbation std grows monotonically
         with depth;
      4. two instances are uncorrelated (R6);
      5. zero allocation (structural).
*/

#include <juce_core/juce_core.h>

#include "dsp/PitchModUnit.h"

#include <cmath>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;

// Zero-crossing frequency measurement (same helper as T06).
float measureFrequency (const std::vector<float>& signal, int centreSample, int halfWindow)
{
    const auto start = centreSample - halfWindow;
    const auto end   = centreSample + halfWindow;

    std::vector<double> crossings;

    for (int n = start + 1; n < end; ++n)
    {
        const auto a = signal[n - 1];
        const auto b = signal[n];

        if ((a < 0.0f) != (b < 0.0f))
        {
            const auto frac = static_cast<double> (a) / (static_cast<double> (a) - b);
            crossings.push_back (static_cast<double> (n - 1) + frac);
        }
    }

    if (crossings.size() < 2)
        return 0.0f;

    const auto span = crossings.back() - crossings.front();
    return static_cast<float> ((crossings.size() - 1) * kSampleRate / (2.0 * span));
}

//==============================================================================
class PitchModUnitTransparencyTest final : public juce::UnitTest
{
public:
    PitchModUnitTransparencyTest() : juce::UnitTest ("PitchModUnit") {}

    void runTest() override
    {
        beginTest ("zero depths are bit-transparent (<= -100 dBFS)");
        {
            dsp::PitchModUnit unit;
            unit.prepare (kSampleRate, 512);

            dsp::PitchModUnit::Params params; // all depths zero by default
            unit.setParameters (params);

            double sumSqDiff = 0.0;
            double sumSqRef  = 0.0;

            for (int n = 0; n < 48000; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi * 1000.0f * n / static_cast<float> (kSampleRate));
                const auto y = unit.process (x);

                sumSqDiff += static_cast<double> (y - x) * (y - x);
                sumSqRef  += static_cast<double> (x) * x;
            }

            const auto snr = 10.0 * std::log10 (sumSqRef / juce::jmax (sumSqDiff, 1e-30));
            expectGreaterThan (snr, 100.0, "expected <= -100 dBFS difference, got SNR " + juce::String (snr));
        }

        beginTest ("20 ms smoothing ramps the amplitude target");
        {
            dsp::PitchModUnit unit;
            unit.prepare (kSampleRate, 512);

            dsp::PitchModUnit::Params params;
            params.vibDepth = 1200.0f; // octave
            params.vibRate  = 10.0f;
            unit.setParameters (params);

            const auto target = static_cast<float> (dsp::ModDelayLine::centsToSeconds (1200.0f, 10.0f) * kSampleRate);

            // ramp length: 20 ms @ 48 kHz = 960 samples
            float value = 0.0f;
            for (int i = 0; i < 960; ++i)
            {
                unit.process (0.0f);
                value = unit.getVibAmpSamples();
            }

            expectWithinAbsoluteError (value, target, target * 1e-3f,
                                       "amplitude did not reach target within 20 ms");
        }

        beginTest ("depth capacity stays within the 8192-sample line (SPEC §5.2)");
        {
            dsp::PitchModUnit unit;
            unit.prepare (kSampleRate, 512);

            dsp::PitchModUnit::Params params;
            params.jitterDepth = 50.0f;   // max
            params.jitterRate  = 5.0f;    // lowest rate -> largest A
            params.vibDepth    = 1200.0f; // max
            params.vibRate     = 10.0f;   // lowest rate -> largest A
            unit.setParameters (params);

            // worst-case depth = 2 * (A_j + A_v)
            const auto aj = static_cast<float> (dsp::ModDelayLine::centsToSeconds (50.0f, 5.0f) * kSampleRate);
            const auto av = static_cast<float> (dsp::ModDelayLine::centsToSeconds (1200.0f, 10.0f) * kSampleRate);

            expectLessThan (2.0f * (aj + av), static_cast<float> (dsp::ModDelayLine::maxDelaySamples));

            // sanity: at 192 kHz the same worst case must also fit (SPEC §5.2)
            const auto aj192 = dsp::ModDelayLine::centsToSeconds (50.0f, 5.0f) * 192000.0;
            const auto av192 = dsp::ModDelayLine::centsToSeconds (1200.0f, 10.0f) * 192000.0;
            expectLessThan (static_cast<float> (2.0 * (aj192 + av192)),
                            static_cast<float> (dsp::ModDelayLine::maxDelaySamples));
        }
    }
};

//==============================================================================
class PitchModVibSwingTest final : public juce::UnitTest
{
public:
    PitchModVibSwingTest() : juce::UnitTest ("PitchModVibSwing") {}

    void runTest() override
    {
        beginTest ("vib-only sine swing matches set cents (+-5%)");
        {
            constexpr float inputFreq = 1000.0f;
            constexpr float vibRate = 10.0f;
            constexpr float vibDepthCents = 100.0f;

            dsp::PitchModUnit unit;
            unit.prepare (kSampleRate, 512);

            dsp::PitchModUnit::Params params;
            params.vibDepth = vibDepthCents;
            params.vibRate  = vibRate;
            params.vibWave  = dsp::ModLFO::Waveform::sine;
            unit.setParameters (params);

            constexpr int totalSamples = 3 * 48000;
            std::vector<float> output (totalSamples);

            for (int n = 0; n < totalSamples; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi * inputFreq * n / static_cast<float> (kSampleRate));
                output[n] = unit.process (x);
            }

            const auto ratio = std::pow (2.0f, vibDepthCents / 1200.0f) - 1.0f;
            const auto freqLow  = inputFreq * (1.0f - ratio);
            const auto freqHigh = inputFreq * (1.0f + ratio);
            const auto tolerance = 3.0f;

            for (int k = 2; k < 28; ++k)
            {
                const auto f = measureFrequency (output, k * 4800, 240);
                expectWithinAbsoluteError (f, freqLow, tolerance,
                                           "low-side swing at k=" + juce::String (k) + ": f=" + juce::String (f));
            }

            for (int k = 2; k < 28; ++k)
            {
                const auto f = measureFrequency (output, (2 * k + 1) * 2400, 240);
                expectWithinAbsoluteError (f, freqHigh, tolerance,
                                           "high-side swing at k=" + juce::String (k) + ": f=" + juce::String (f));
            }
        }
    }
};

//==============================================================================
class PitchModJitterTest final : public juce::UnitTest
{
public:
    PitchModJitterTest() : juce::UnitTest ("PitchModJitter") {}

    void runTest() override
    {
        beginTest ("jitter-only perturbation std grows monotonically with depth");
        {
            const auto measureStd = [] (float depthCents)
            {
                constexpr float inputFreq = 1000.0f;

                dsp::PitchModUnit unit;
                unit.prepare (kSampleRate, 512);
                unit.setSeed (12345);

                dsp::PitchModUnit::Params params;
                params.jitterDepth = depthCents;
                params.jitterRate  = 25.0f;
                unit.setParameters (params);

                constexpr int totalSamples = 4 * 48000;
                std::vector<float> output (totalSamples);

                for (int n = 0; n < totalSamples; ++n)
                {
                    const auto x = std::sin (juce::MathConstants<float>::twoPi * inputFreq * n / static_cast<float> (kSampleRate));
                    output[n] = unit.process (x);
                }

                // short-time frequency: 20 ms window, 10 ms step
                constexpr int window = 960;
                constexpr int step = 480;

                std::vector<float> freqs;
                for (int c = window; c + window < totalSamples; c += step)
                    freqs.push_back (measureFrequency (output, c, window / 2));

                double mean = 0.0;
                for (auto f : freqs) mean += f;
                mean /= freqs.size();

                double var = 0.0;
                for (auto f : freqs) var += (f - mean) * (f - mean);
                var /= freqs.size();

                return static_cast<float> (std::sqrt (var));
            };

            const auto std0  = measureStd (0.0f);
            const auto std10 = measureStd (10.0f);
            const auto std25 = measureStd (25.0f);
            const auto std50 = measureStd (50.0f);

            expectLessThan (std0, 0.1f, "zero depth should have ~zero perturbation");
            expectGreaterThan (std10, std0, "std(10) should exceed std(0)");
            expectGreaterThan (std25, std10, "std(25) should exceed std(10)");
            expectGreaterThan (std50, std25, "std(50) should exceed std(25)");
        }
    }
};

//==============================================================================
class PitchModCorrelationTest final : public juce::UnitTest
{
public:
    PitchModCorrelationTest() : juce::UnitTest ("PitchModCorrelation") {}

    void runTest() override
    {
        beginTest ("two instances are uncorrelated (R6)");
        {
            // Compare the *modulation depth* sequences (the audio outputs would
            // be dominated by the shared deterministic input). Jitter only:
            // vibration is a deterministic waveform and must NOT be required
            // to decorrelate channels (R6 covers random sources).
            dsp::PitchModUnit a, b;
            a.prepare (kSampleRate, 512);
            b.prepare (kSampleRate, 512);
            a.setSeed (111);
            b.setSeed (222);

            dsp::PitchModUnit::Params params;
            params.jitterDepth = 50.0f;
            params.jitterRate  = 25.0f;
            a.setParameters (params);
            b.setParameters (params);

            constexpr int numSamples = 1920000; // 40 s
            double sumA = 0.0, sumB = 0.0, sumAB = 0.0, sumA2 = 0.0, sumB2 = 0.0;

            for (int n = 0; n < numSamples; ++n)
            {
                a.process (0.0f);
                b.process (0.0f);

                const auto va = static_cast<double> (a.getLastDepth());
                const auto vb = static_cast<double> (b.getLastDepth());
                sumA += va;
                sumB += vb;
                sumAB += va * vb;
                sumA2 += va * va;
                sumB2 += vb * vb;
            }

            const auto n = static_cast<double> (numSamples);
            const auto cov = (sumAB - sumA * sumB / n) / n;
            const auto varA = (sumA2 - sumA * sumA / n) / n;
            const auto varB = (sumB2 - sumB * sumB / n) / n;
            const auto rho = static_cast<float> (cov / std::sqrt (varA * varB));

            expect (std::abs (rho) < 0.05f, "instances are correlated: rho = " + juce::String (rho));
        }
    }
};

PitchModUnitTransparencyTest pitchModUnitTransparencyTest;
PitchModVibSwingTest pitchModVibSwingTest;
PitchModJitterTest pitchModJitterTest;
PitchModCorrelationTest pitchModCorrelationTest;

//==============================================================================
class PitchModClampTest final : public juce::UnitTest
{
public:
    PitchModClampTest() : juce::UnitTest ("PitchModClamp") {}

    void runTest() override
    {
        beginTest ("out-of-range depth/rate inputs are clamped, not corrupted (O3)");
        {
            dsp::PitchModUnit unit;
            unit.prepare (48000.0, 512);
            unit.setSeed (1);

            dsp::PitchModUnit::Params p;
            p.jitterDepth = 5000.0f; // far beyond the 1200 cent spec range
            p.jitterRate  = 0.0f;    // would make centsToSeconds divide by zero
            p.vibDepth    = -100.0f;
            p.vibRate     = 0.0f;
            unit.setParameters (p);

            bool finite = true;
            float maxAbs = 0.0f;
            for (int n = 0; n < 48000; ++n)
            {
                const auto y = unit.process (0.3f);
                if (! std::isfinite (y))
                    finite = false;
                maxAbs = juce::jmax (maxAbs, std::abs (y));
            }

            expect (finite, "corrupted parameters produced NaN/inf");
            expectLessThan (maxAbs, 10.0f, "clamped depth still blew up the delay line");

            // The clamped amplitude must be finite and far below what the
            // unclamped input would produce (5000 cents at 0 Hz rate would be
            // a divide-by-zero / seconds of delay).  The delay line itself
            // additionally clamps to its capacity (SPEC §5.2).
            expectLessThan (unit.getJitterAmpSamples(), 100000.0f,
                            "jitter amplitude not clamped to sane bounds");
        }
    }
};

PitchModClampTest pitchModClampTest;

} // namespace
