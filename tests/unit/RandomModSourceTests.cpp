/*
    NeoVoiDrive T05 tests — RandomModSource statistical properties.

    Acceptance (TASKS.md T05):
      1. output bounded in [-1, 1];
      2. mean ~ 0 (+-0.05);
      3. effective update rate ~ set rate (+-10%) — verified two ways:
         a) getHoldCount() S&H clock (exact);
         b) output-level mean absolute difference (MAD) scales linearly with
            rate (proportionality, not the absolute formula, to stay black-box);
      4. two instances are uncorrelated (|rho| < 0.05, R6);
      5. zero allocation (structural: no heap types in the class).

    Fixed RNG seeds make every statistical assertion deterministic.
*/

#include <juce_core/juce_core.h>

#include "dsp/RandomModSource.h"

#include <cmath>
#include <limits>

namespace
{

constexpr double sampleRate = 48000.0;

void run (dsp::RandomModSource& source, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
        source.next();
}

//==============================================================================
class RandomModSourceTest final : public juce::UnitTest
{
public:
    RandomModSourceTest() : juce::UnitTest ("RandomModSource") {}

    void runTest() override
    {
        beginTest ("output is bounded in [-1, 1]");
        {
            dsp::RandomModSource source;
            source.prepare (sampleRate);
            source.setSeed (1);
            source.setRate (25.0f);

            bool inBounds = true;
            for (int i = 0; i < 480000; ++i) // 10 s
            {
                const auto v = source.next();
                if (! (v >= -1.0f && v <= 1.0f))
                {
                    inBounds = false;
                    break;
                }
            }

            expect (inBounds, "output escaped [-1, 1]");
        }

        beginTest ("mean is approximately zero (+-0.05)");
        {
            dsp::RandomModSource source;
            source.prepare (sampleRate);
            source.setSeed (2);
            source.setRate (25.0f);

            constexpr int numSamples = 1920000; // 40 s
            double sum = 0.0;
            for (int i = 0; i < numSamples; ++i)
                sum += source.next();

            const auto mean = static_cast<float> (sum / numSamples);
            expectWithinAbsoluteError (mean, 0.0f, 0.05f, "mean drifted from 0");
        }

        beginTest ("S&H clock matches the set rate (+-1 draw)");
        {
            const auto check = [&] (float rateHz, int durationSeconds, int expectedDraws)
            {
                dsp::RandomModSource source;
                source.prepare (sampleRate);
                source.setSeed (3);
                source.setRate (rateHz);

                run (source, static_cast<int> (sampleRate * durationSeconds));

                const auto draws = static_cast<int> (source.getHoldCount());
                expectWithinAbsoluteError (static_cast<float> (draws), static_cast<float> (expectedDraws), 1.0f,
                                           juce::String (rateHz) + " Hz: draw count mismatch");
            };

            check (5.0f,   2, 10);
            check (25.0f,  2, 50);
            check (100.0f, 1, 100);
            check (300.0f, 1, 300);
        }

        beginTest ("output update rate scales linearly with set rate (MAD)");
        {
            // Mean absolute difference of the output is proportional to the
            // smoothing step, which is proportional to `rate`; doubling the
            // rate must double the MAD (black-box proportionality check).
            const auto measureMAD = [] (float rateHz)
            {
                dsp::RandomModSource source;
                source.prepare (sampleRate);
                source.setSeed (4);
                source.setRate (rateHz);

                double sumAbsDelta = 0.0;
                float previous = source.next();
                for (int i = 1; i < 1920000; ++i) // 40 s
                {
                    const auto current = source.next();
                    sumAbsDelta += std::abs (static_cast<double> (current) - previous);
                    previous = current;
                }

                return static_cast<float> (sumAbsDelta / 1919999.0);
            };

            const auto mad25 = measureMAD (25.0f);
            const auto mad50 = measureMAD (50.0f);

            expectGreaterThan (mad25, 0.0f);
            const auto ratio = mad50 / mad25;
            expectWithinAbsoluteError (ratio, 2.0f, 0.2f, "MAD did not scale with rate");
        }

        beginTest ("two instances are uncorrelated (independent RNGs, R6)");
        {
            // Fixed distinct seeds -> deterministic correlation measurement.
            dsp::RandomModSource a;
            dsp::RandomModSource b;
            a.prepare (sampleRate);
            b.prepare (sampleRate);
            a.setSeed (11); // fixed distinct seeds -> deterministic rho
            b.setSeed (22);
            a.setRate (25.0f);
            b.setRate (25.0f);

            constexpr int numSamples = 1920000; // 40 s
            double sumA = 0.0, sumB = 0.0, sumAB = 0.0, sumA2 = 0.0, sumB2 = 0.0;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto va = static_cast<double> (a.next());
                const auto vb = static_cast<double> (b.next());
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

        beginTest ("rate changes are click-free and bounded");
        {
            dsp::RandomModSource source;
            source.prepare (sampleRate);
            source.setSeed (5);
            source.setRate (25.0f);

            // warm up
            run (source, 48000);

            // slam the rate mid-stream; output must stay bounded and the
            // per-sample step must never jump (no click)
            float previous = source.next();
            bool bounded = true;
            bool noClick = true;

            for (int i = 1; i < 480000; ++i)
            {
                if (i == 96000)
                    source.setRate (250.0f); // 10x change mid-stream

                const auto current = source.next();

                if (! (current >= -1.0f && current <= 1.0f))
                    bounded = false;
                if (std::abs (current - previous) > 0.5f) // one-pole step is tiny
                    noClick = false;

                previous = current;
            }

            expect (bounded, "output escaped [-1, 1] after rate change");
            expect (noClick, "output jumped after rate change");
        }

        beginTest ("defensive clamping of invalid rate values");
        {
            dsp::RandomModSource source;
            source.prepare (sampleRate);
            source.setSeed (6);

            // non-positive and non-finite rates must not break the signal
            source.setRate (0.0f);
            source.setRate (-25.0f);
            source.setRate (std::numeric_limits<float>::infinity());
            source.setRate (std::numeric_limits<float>::quiet_NaN());

            bool bounded = true;
            for (int i = 0; i < 48000; ++i)
            {
                const auto v = source.next();
                if (! (v >= -1.0f && v <= 1.0f))
                    bounded = false;
            }

            expect (bounded, "output broke after invalid setRate calls");
        }

        beginTest ("high sample rate and rate stay bounded and on-clock");
        {
            constexpr double sr192 = 192000.0;

            dsp::RandomModSource source;
            source.prepare (sr192);
            source.setSeed (7);
            source.setRate (300.0f);

            run (source, static_cast<int> (sr192)); // 1 s @ 192 kHz

            expectEquals (static_cast<int> (source.getHoldCount()), 300);

            bool bounded = true;
            for (int i = 0; i < static_cast<int> (sr192); ++i)
            {
                const auto v = source.next();
                if (! (v >= -1.0f && v <= 1.0f))
                    bounded = false;
            }
            expect (bounded, "output escaped [-1, 1] at 192 kHz / 300 Hz");
        }
    }
};

RandomModSourceTest randomModSourceTest;

} // namespace
