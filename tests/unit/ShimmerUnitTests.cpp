/*
    NeoVoiDrive T08 tests — ShimmerUnit gain modulation.

    Acceptance (TASKS.md T08):
      1. shimmer_depth=0 -> bit-transparent;
      2. gain coefficient bounded in [0, 1] (and [1-depth, 1] for the set
         depth);
      3. modulation rate ~ set rate (S&H clock + boundedness);
      4. two instances are uncorrelated (R6);
      5. zero allocation (structural).

    Fixed RNG seeds make every statistical assertion deterministic.
*/

#include <juce_core/juce_core.h>

#include "dsp/ShimmerUnit.h"

#include <cmath>
#include <limits>

namespace
{

constexpr double kSampleRate = 48000.0;

//==============================================================================
class ShimmerUnitTest final : public juce::UnitTest
{
public:
    ShimmerUnitTest() : juce::UnitTest ("ShimmerUnit") {}

    void runTest() override
    {
        beginTest ("zero depth is bit-transparent");
        {
            dsp::ShimmerUnit unit;
            unit.prepare (kSampleRate);

            dsp::ShimmerUnit::Params params; // depth 0 by default
            unit.setParameters (params);

            bool identical = true;
            for (int n = 0; n < 48000; ++n)
            {
                const auto x = std::sin (0.01f * static_cast<float> (n));
                if (unit.process (x) != x)
                {
                    identical = false;
                    break;
                }
            }

            expect (identical, "zero depth must be bit-transparent");
        }

        beginTest ("gain stays within [1 - depth/100, 1]");
        {
            const auto checkBounds = [this] (float depth, float expectedMin)
            {
                dsp::ShimmerUnit unit;
                unit.prepare (kSampleRate);
                unit.setSeed (42);

                dsp::ShimmerUnit::Params params;
                params.depth = depth;
                params.rate  = 25.0f;
                unit.setParameters (params);

                float minGain = std::numeric_limits<float>::max();
                float maxGain = std::numeric_limits<float>::lowest();

                for (int i = 0; i < 96000; ++i) // 2 s (let smoothing settle too)
                {
                    unit.process (0.5f);
                    minGain = juce::jmin (minGain, unit.getLastGain());
                    maxGain = juce::jmax (maxGain, unit.getLastGain());
                }

                expectGreaterThan (minGain, expectedMin - 0.01f,
                                   juce::String (depth) + "% depth: gain fell below " + juce::String (expectedMin));
                expectLessThan (maxGain, 1.0f + 1e-6f);
            };

            checkBounds (0.0f,  1.0f);
            checkBounds (25.0f, 0.75f);
            checkBounds (50.0f, 0.5f);
            checkBounds (100.0f, 0.0f);
        }

        beginTest ("full depth dips near zero and reaches near unity");
        {
            // Note: g > 0 strictly (m01 < 1); the assertion checks that the
            // modulation actually spans the usable range of the gain.
            dsp::ShimmerUnit unit;
            unit.prepare (kSampleRate);
            unit.setSeed (42);

            dsp::ShimmerUnit::Params params;
            params.depth = 100.0f;
            params.rate  = 25.0f;
            unit.setParameters (params);

            float minGain = 1.0f;
            float maxGain = 0.0f;
            for (int i = 0; i < 192000; ++i) // 4 s
            {
                unit.process (0.5f);
                minGain = juce::jmin (minGain, unit.getLastGain());
                maxGain = juce::jmax (maxGain, unit.getLastGain());
            }

            expectLessThan (minGain, 0.05f, "full depth should dip near zero: min=" + juce::String (minGain));
            expectGreaterThan (maxGain, 0.95f, "full depth should reach near unity: max=" + juce::String (maxGain));
        }

        beginTest ("modulation rate matches the S&H clock (rate Hz)");
        {
            const auto checkClock = [this] (float rateHz, int durationSeconds, int expectedDraws)
            {
                dsp::ShimmerUnit unit;
                unit.prepare (kSampleRate);
                unit.setSeed (7);

                dsp::ShimmerUnit::Params params;
                params.depth = 50.0f;
                params.rate  = rateHz;
                unit.setParameters (params);

                for (int i = 0; i < static_cast<int> (kSampleRate * durationSeconds); ++i)
                    unit.process (0.5f);

                expectWithinAbsoluteError (static_cast<float> (unit.getHoldCount()),
                                           static_cast<float> (expectedDraws), 1.0f,
                                           juce::String (rateHz) + " Hz clock mismatch");
            };

            checkClock (5.0f,  2, 10);
            checkClock (25.0f, 2, 50);
            checkClock (100.0f, 1, 100);
        }

        beginTest ("depth smoothing ramps over 20 ms");
        {
            dsp::ShimmerUnit unit;
            unit.prepare (kSampleRate);

            dsp::ShimmerUnit::Params params;
            params.depth = 100.0f;
            unit.setParameters (params);

            for (int i = 0; i < 960; ++i) // 20 ms @ 48 kHz
                unit.process (0.0f);

            expectWithinAbsoluteError (unit.getSmoothedDepth(), 100.0f, 0.2f,
                                       "smoothed depth did not reach target within 20 ms");
        }

        beginTest ("two instances are uncorrelated (R6)");
        {
            dsp::ShimmerUnit a, b;
            a.prepare (kSampleRate);
            b.prepare (kSampleRate);
            a.setSeed (111);
            b.setSeed (222);

            dsp::ShimmerUnit::Params params;
            params.depth = 80.0f;
            params.rate  = 25.0f;
            a.setParameters (params);
            b.setParameters (params);

            constexpr int numSamples = 1920000; // 40 s
            double sumA = 0.0, sumB = 0.0, sumAB = 0.0, sumA2 = 0.0, sumB2 = 0.0;

            for (int n = 0; n < numSamples; ++n)
            {
                a.process (0.0f);
                b.process (0.0f);

                const auto ga = static_cast<double> (a.getLastGain());
                const auto gb = static_cast<double> (b.getLastGain());
                sumA += ga;
                sumB += gb;
                sumAB += ga * gb;
                sumA2 += ga * ga;
                sumB2 += gb * gb;
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

ShimmerUnitTest shimmerUnitTest;

} // namespace
