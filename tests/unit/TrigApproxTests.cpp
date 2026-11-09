/*
    TRIG-AB / P3: Padé sin/cos/tanh call sites used by the three per-sample
    DSP hot paths. These bounds are the measured approximation error, not
    audio tolerances (the full-chain A/B is in docs/test_report.md).
*/

#include <juce_core/juce_core.h>

#include "dsp/TrigApprox.h"

#include <cmath>

namespace
{

class TrigApproxTests final : public juce::UnitTest
{
public:
    TrigApproxTests() : juce::UnitTest ("TrigApprox") {}

    void runTest() override
    {
        using dsp::trig::sin;
        using dsp::trig::cos;
        using dsp::trig::tanh;

        beginTest ("sin/cos track std:: over the full phase range");
        {
            float maxSin = 0.0f;
            float maxCos = 0.0f;

            for (int i = 0; i <= 20000; ++i)
            {
                const float x = -100.0f + 200.0f * static_cast<float> (i) / 20000.0f;
                maxSin = juce::jmax (maxSin, std::abs (sin (x) - std::sin (x)));
                maxCos = juce::jmax (maxCos, std::abs (cos (x) - std::cos (x)));
            }

            expectWithinAbsoluteError (maxSin, 0.0f, 3.0e-5f,
                                       "sin approximation left its measured error band");
            expectWithinAbsoluteError (maxCos, 0.0f, 1.2e-4f,
                                       "cos approximation left its measured error band");
            logMessage ("  max |sin err| = " + juce::String (maxSin)
                        + ", max |cos err| = " + juce::String (maxCos));
        }

        beginTest ("tanh tracks std:: with the documented input clamp");
        {
            float maxErr = 0.0f;

            for (int i = 0; i <= 20000; ++i)
            {
                const float x = -20.0f + 40.0f * static_cast<float> (i) / 20000.0f;
                maxErr = juce::jmax (maxErr, std::abs (tanh (x) - std::tanh (x)));
            }

            expectWithinAbsoluteError (maxErr, 0.0f, 2.0e-4f,
                                       "tanh approximation left its measured error band");
            expectWithinAbsoluteError (tanh (0.0f), 0.0f, 1.0e-7f);
            expectWithinAbsoluteError (tanh (5.0f), std::tanh (5.0f), 2.0e-4f);
            expectWithinAbsoluteError (tanh (-5.0f), std::tanh (-5.0f), 2.0e-4f);
            logMessage ("  max |tanh err| = " + juce::String (maxErr));
        }

        beginTest ("phase reduction keeps sin/cos finite and periodic");
        {
            const float twoPi = juce::MathConstants<float>::twoPi;

            for (const float base : { 0.0f, 0.7f, -2.3f, 47.0f, -123.0f })
            {
                const float a = dsp::trig::sin (base);
                const float b = dsp::trig::sin (base + twoPi * 3.0f);
                const float c = dsp::trig::cos (base);
                const float d = dsp::trig::cos (base - twoPi * 5.0f);

                expectWithinAbsoluteError (a, b, 2.0e-4f);
                expectWithinAbsoluteError (c, d, 2.0e-4f);
                expect (std::isfinite (a) && std::isfinite (c));
            }

            // wrapToPi must stay inside [-pi, pi].
            for (const float x : { -200.0f, -7.0f, 7.0f, 200.0f })
            {
                const float w = dsp::trig::wrapToPi (x);
                expect (w >= -juce::MathConstants<float>::pi
                        && w <= juce::MathConstants<float>::pi);
            }
        }
    }
};

TrigApproxTests trigApproxTests;

} // namespace
