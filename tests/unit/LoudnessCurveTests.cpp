/*
    NeoVoiDrive T71 tests — loudness knob control law (band gain).

    The approved feel:
      - 50% -> 0 dB, 100% -> +12 dB;
      - 0% -> mute (-inf; stored as the -48 dB sentinel);
      - attenuation 0.08|d| + 0.92|d|^3 (slow near center, fast near 0%);
      - boost 0.80 d + 0.20 d^3 (near-linear, slight acceleration).
*/

#include <juce_core/juce_core.h>

#include "PluginProcessor.h"
#include "params/LoudnessCurve.h"

#include <cmath>

namespace
{

class LoudnessCurveTest final : public juce::UnitTest
{
public:
    LoudnessCurveTest() : juce::UnitTest ("LoudnessCurve") {}

    void runTest() override
    {
        beginTest ("normalised positions follow the approved dB curve");
        {
            using namespace params::loudness;

            expectWithinAbsoluteError (dbFromNorm (0.50f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (dbFromNorm (1.00f), 12.0f, 1.0e-6f);
            expectWithinAbsoluteError (dbFromNorm (0.00f), -48.0f, 1.0e-6f);

            // Attenuation side (slow start / accelerating).
            expectWithinAbsoluteError (dbFromNorm (0.40f), -1.12f, 0.02f);
            expectWithinAbsoluteError (dbFromNorm (0.25f), -7.44f, 0.02f);
            expectWithinAbsoluteError (dbFromNorm (0.10f), -25.68f, 0.05f);
            expectWithinAbsoluteError (dbFromNorm (0.05f), -35.65f, 0.05f);
            expectWithinAbsoluteError (dbFromNorm (0.01f), -45.33f, 0.05f);

            // Boost side (near-linear).
            expectWithinAbsoluteError (dbFromNorm (0.75f), 5.10f, 0.02f);
            expectWithinAbsoluteError (dbFromNorm (0.95f), 10.39f, 0.05f);

            // Monotonic over the full travel.
            float previous = dbFromNorm (0.0f);
            for (int i = 1; i <= 1000; ++i)
            {
                const auto db = dbFromNorm (static_cast<float> (i) / 1000.0f);
                expect (db >= previous - 1.0e-6f,
                        "loudness curve is not monotonic at " + juce::String (i));
                previous = db;
            }
        }

        beginTest ("normalisable range round-trips and exposes the mute sentinel");
        {
            using namespace params::loudness;
            auto range = makeRange();

            expectWithinAbsoluteError (range.convertFrom0to1 (0.5f), 0.0f, 1.0e-5f);
            expectWithinAbsoluteError (range.convertTo0to1 (0.0f), 0.5f, 1.0e-5f);
            expectWithinAbsoluteError (range.convertFrom0to1 (1.0f), 12.0f, 1.0e-5f);
            expectWithinAbsoluteError (range.convertTo0to1 (12.0f), 1.0f, 1.0e-5f);
            expectWithinAbsoluteError (range.convertTo0to1 (-48.0f), 0.0f, 1.0e-6f);

            for (float db : { -47.5f, -40.0f, -24.0f, -12.0f, -3.0f, 0.0f,
                              1.0f, 5.1f, 9.0f, 11.9f })
            {
                const auto norm = normFromDb (db);
                expectWithinAbsoluteError (dbFromNorm (norm), db, 0.002f,
                                           "dB round-trip at " + juce::String (db));
            }

            expect (isMuteDb (-48.0f), "stored -48 dB must be the mute sentinel");
            expect (! isMuteDb (-47.9f), "-47.9 dB must remain audible");
            expectWithinAbsoluteError (dspTargetDb (-48.0f), internalMuteDb, 1.0e-6f);
            expectWithinAbsoluteError (dspTargetDb (-24.0f), -24.0f, 1.0e-6f);
        }

        beginTest ("band gain parameters use the loudness range");
        {
            PluginProcessor processor;

            for (const auto* id : { "b1_gain", "b2_gain", "b3_gain" })
            {
                auto* f = dynamic_cast<juce::AudioParameterFloat*> (
                    processor.apvts.getParameter (id));
                expect (f != nullptr, juce::String (id) + " missing");
                if (f == nullptr)
                    continue;

                auto& r = f->getNormalisableRange();
                expectWithinAbsoluteError (r.start, -48.0f, 1.0e-6f);
                expectWithinAbsoluteError (r.end, 12.0f, 1.0e-6f);
                expectWithinAbsoluteError (r.convertFrom0to1 (0.5f), 0.0f, 1.0e-5f);
                expectWithinAbsoluteError (r.convertFrom0to1 (0.25f), -7.44f, 0.02f);
                expectWithinAbsoluteError (r.convertFrom0to1 (0.75f), 5.10f, 0.02f);
                expectWithinAbsoluteError (r.convertFrom0to1 (1.0f), 12.0f, 1.0e-5f);
                expectWithinAbsoluteError (r.convertTo0to1 (0.0f), 0.5f, 1.0e-5f);
                expectWithinAbsoluteError (r.convertTo0to1 (-48.0f), 0.0f, 1.0e-6f);
            }
        }
    }
};

LoudnessCurveTest loudnessCurveTest;

} // namespace
