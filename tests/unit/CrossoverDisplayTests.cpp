#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>

#include "PluginProcessor.h"
#include "dsp/CrossoverBank.h"
#include "ui/CrossoverDisplay.h"

namespace
{

// T17: pure-logic tests for the CrossoverDisplay helpers. The interactive
// parts (drag, painting, 30 fps timer) are manual-UI acceptance (task card).
class CrossoverDisplayTests final : public juce::UnitTest
{
public:
    CrossoverDisplayTests() : juce::UnitTest ("CrossoverDisplay") {}

    void runTest() override
    {
        beginTest ("log frequency <-> x mapping round-trips");
        {
            constexpr float width = 800.0f;

            expectWithinAbsoluteError (ui::CrossoverDisplay::freqToX (40.0f, width), 0.0f, 0.01f,
                                       "40 Hz maps to the left edge");
            expectWithinAbsoluteError (ui::CrossoverDisplay::freqToX (18000.0f, width), width, 0.01f,
                                       "18k Hz maps to the right edge");

            for (float f : { 40.0f, 100.0f, 200.0f, 1000.0f, 5000.0f, 18000.0f })
            {
                const float back = ui::CrossoverDisplay::xToFreq (
                    ui::CrossoverDisplay::freqToX (f, width), width);
                expect (std::abs (back - f) / f < 0.005f,
                        juce::String ("round-trip ") + juce::String (f)
                            + " Hz -> " + juce::String (back) + " Hz");
            }

            // Out-of-range frequencies clamp to the edges.
            expectWithinAbsoluteError (ui::CrossoverDisplay::freqToX (10.0f, width), 0.0f, 0.01f,
                                       "10 Hz clamps to the left edge");
            expectWithinAbsoluteError (ui::CrossoverDisplay::freqToX (50000.0f, width), width, 0.01f,
                                       "50k Hz clamps to the right edge");
        }

        beginTest ("T70: spectrum and envelope use separate dB mappings");
        {
            constexpr float top = 10.0f;
            constexpr float bottom = 110.0f;
            constexpr float mid = (top + bottom) * 0.5f;

            // Spectrum: 0 dBFS at the top, -72 dBFS at the bottom.
            expectWithinAbsoluteError (
                ui::CrossoverDisplay::spectrumDbToY (0.0f, top, bottom), top, 0.01f);
            expectWithinAbsoluteError (
                ui::CrossoverDisplay::spectrumDbToY (-72.0f, top, bottom), bottom, 0.01f);
            expectWithinAbsoluteError (
                ui::CrossoverDisplay::spectrumDbToY (-36.0f, top, bottom), mid, 0.01f);

            // Envelope/grid: +18 dB at the top, -18 dB at the bottom.
            expectWithinAbsoluteError (
                ui::CrossoverDisplay::envelopeDbToY (18.0f, top, bottom), top, 0.01f);
            expectWithinAbsoluteError (
                ui::CrossoverDisplay::envelopeDbToY (-18.0f, top, bottom), bottom, 0.01f);
            expectWithinAbsoluteError (
                ui::CrossoverDisplay::envelopeDbToY (0.0f, top, bottom), mid, 0.01f);

            // The two scales must differ: -36 dB is mid-graph for the
            // spectrum but below the envelope's bottom.
            expect (ui::CrossoverDisplay::spectrumDbToY (-36.0f, top, bottom)
                        < ui::CrossoverDisplay::envelopeDbToY (-36.0f, top, bottom),
                    "spectrum and envelope mappings collapsed to one scale");
        }

        beginTest ("crossover clamping enforces xover1 < xover2");
        {
            // Audit P2-7: the UI clamp mirrors the DSP's 1.05x ratio.
            expectWithinAbsoluteError (ui::CrossoverDisplay::clampXover1 (30000.0f, 500.0f),
                                       500.0f / dsp::CrossoverBank::minFreqRatio, 0.001f,
                                       "xover1 above the limit -> xover2 / 1.05");
            expectWithinAbsoluteError (ui::CrossoverDisplay::clampXover1 (20.0f, 500.0f),
                                       40.0f, 0.001f, "xover1 below the floor -> 40");
            expectWithinAbsoluteError (ui::CrossoverDisplay::clampXover1 (250.0f, 500.0f),
                                       250.0f, 0.001f, "xover1 in range unchanged");

            expectWithinAbsoluteError (ui::CrossoverDisplay::clampXover2 (20.0f, 500.0f),
                                       500.0f * dsp::CrossoverBank::minFreqRatio, 0.001f,
                                       "xover2 below the limit -> xover1 * 1.05");
            expectWithinAbsoluteError (ui::CrossoverDisplay::clampXover2 (40000.0f, 500.0f),
                                       18000.0f, 0.001f, "xover2 above the ceiling -> 18k");
            expectWithinAbsoluteError (ui::CrossoverDisplay::clampXover2 (1000.0f, 500.0f),
                                       1000.0f, 0.001f, "xover2 in range unchanged");

            // P2-7: controls that the band count cannot use are disabled.
            expect (! ui::CrossoverDisplay::xoverKnobApplies (0, 1), "Xover 1 in 1-band mode");
            expect (ui::CrossoverDisplay::xoverKnobApplies (0, 2), "Xover 1 in 2-band mode");
            expect (! ui::CrossoverDisplay::xoverKnobApplies (1, 2), "Xover 2 in 2-band mode");
            expect (ui::CrossoverDisplay::xoverKnobApplies (1, 3), "Xover 2 in 3-band mode");
            expect (ui::CrossoverDisplay::bandGainApplies (0, 1), "Band 1 gain in 1-band mode");
            expect (! ui::CrossoverDisplay::bandGainApplies (1, 1), "Band 2 gain in 1-band mode");
            expect (ui::CrossoverDisplay::bandGainApplies (1, 2), "Band 2 gain in 2-band mode");
            expect (! ui::CrossoverDisplay::bandGainApplies (2, 2), "Band 3 gain in 2-band mode");
            expect (ui::CrossoverDisplay::bandGainApplies (2, 3), "Band 3 gain in 3-band mode");
        }

        beginTest ("P2-7 band-count-dependent controls are disabled in the UI");
        {
            PluginProcessor processor;
            ui::SpectrumSource spectrumIn, spectrumOut;
            ui::CrossoverDisplay display (processor.apvts, spectrumIn, spectrumOut);

            auto* xover1 = display.findChildWithID ("xover1Knob");
            auto* xover2 = display.findChildWithID ("xover2Knob");
            auto* gain1 = display.findChildWithID ("bandGainKnob1");
            auto* gain2 = display.findChildWithID ("bandGainKnob2");
            auto* gain3 = display.findChildWithID ("bandGainKnob3");

            expect (xover1 != nullptr && xover2 != nullptr && gain1 != nullptr
                        && gain2 != nullptr && gain3 != nullptr,
                    "crossover control component IDs missing");

            if (xover1 == nullptr || xover2 == nullptr || gain1 == nullptr
                || gain2 == nullptr || gain3 == nullptr)
                return;

            const auto setBands = [&] (int index)
            {
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (
                        processor.apvts.getParameter ("band_count")))
                    *p = index;
                display.updateControlEnablementForTest();
            };

            setBands (0); // 1 band
            expect (! xover1->isEnabled() && ! xover2->isEnabled(),
                    "xover knobs must be disabled with 1 band");
            expect (gain1->isEnabled() && ! gain2->isEnabled() && ! gain3->isEnabled(),
                    "only Band 1 gain is active with 1 band");

            setBands (1); // 2 bands
            expect (xover1->isEnabled() && ! xover2->isEnabled(),
                    "Xover 1 active / Xover 2 inactive with 2 bands");
            expect (gain1->isEnabled() && gain2->isEnabled() && ! gain3->isEnabled(),
                    "Band 3 gain must be disabled with 2 bands");

            setBands (2); // 3 bands
            expect (xover1->isEnabled() && xover2->isEnabled(),
                    "both xover knobs must be active with 3 bands");
            expect (gain1->isEnabled() && gain2->isEnabled() && gain3->isEnabled(),
                    "all band gains must be active with 3 bands");
        }

        beginTest ("windowed FFT peak lands on the input frequency");
        {
            // Regression guard: the spectrum path must analyse the windowed
            // input (in-place FFT semantics), not stale buffer contents.
            constexpr int fftSize = 2048;
            constexpr float sampleRate = 48000.0f;

            juce::dsp::FFT fft (11);
            std::array<float, fftSize> timeIn {};
            std::array<float, fftSize / 2> mag {};

            constexpr double twoPi = 6.283185307179586;
            constexpr float freqHz = 1000.0f;
            for (int i = 0; i < fftSize; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (twoPi * i / (fftSize - 1));
                timeIn[static_cast<size_t> (i)] = static_cast<float> (
                    std::sin (twoPi * freqHz * i / sampleRate) * w);
            }

            ui::CrossoverDisplay::computeMagnitude (fft, timeIn.data(), mag.data());

            int peak = 0;
            for (int i = 1; i < fftSize / 2; ++i)
                if (mag[static_cast<size_t> (i)] > mag[static_cast<size_t> (peak)])
                    peak = i;

            // 1000 Hz @ 48000/2048 per bin -> bin 42.67 -> expect 42 or 43.
            expect (peak >= 40 && peak <= 46,
                    juce::String ("spectrum peak at bin ") + juce::String (peak)
                        + " (expected ~43 for 1 kHz)");

            // The peak must dominate the rest (sanity: not all-zero/garbage).
            float sum = 0.0f;
            for (float m : mag)
                sum += m;
            expect (mag[static_cast<size_t> (peak)] > 8.0f * sum / static_cast<float> (fftSize / 2),
                    "spectrum peak is not dominant (FFT path broken?)");
        }
    }
};

CrossoverDisplayTests crossoverDisplayTests;

} // namespace
