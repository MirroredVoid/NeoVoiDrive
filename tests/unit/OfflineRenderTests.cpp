#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>

#include "offline_render/RenderCore.h"

namespace
{

// T21 smoke tests for the offline render core (no file IO — the render loop
// is exercised directly on buffers; the CLI + wav IO path is covered by the
// ctest `offline_smoke` python script).
class OfflineRenderTests final : public juce::UnitTest
{
public:
    OfflineRenderTests() : juce::UnitTest ("OfflineRender") {}

    void runTest() override
    {
        beginTest ("renders a buffer with finite, non-trivial output");
        {
            constexpr int sr = 48000;
            constexpr int seconds = 2;
            juce::AudioBuffer<float> input (2, sr * seconds);
            for (int i = 0; i < input.getNumSamples(); ++i)
            {
                const auto t = static_cast<double> (i) / sr;
                input.setSample (0, i, static_cast<float> (0.3 * std::sin (2.0 * juce::MathConstants<double>::twoPi * 440.0 * t)));
                input.setSample (1, i, static_cast<float> (0.3 * std::sin (2.0 * juce::MathConstants<double>::twoPi * 554.0 * t)));
            }

            offline::RenderOptions options;
            options.sampleRate = sr;
            options.blockSize = 512;

            juce::AudioBuffer<float> output;
            const auto n = offline::renderBuffer (output, input, options);

            expectEquals (n, sr * seconds, "full input should be rendered");
            expectEquals (output.getNumChannels(), 2, "stereo output");
            expectEquals (output.getNumSamples(), sr * seconds, "output length");

            bool finite = true;
            double sumSq = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < output.getNumSamples(); ++i)
                {
                    const auto v = output.getSample (ch, i);
                    finite = finite && std::isfinite (v);
                    sumSq += static_cast<double> (v) * v;
                }
            expect (finite, "output contains non-finite samples");
            expect (sumSq > 1.0, "output is silent (energy "
                                     + juce::String (sumSq) + ")");
        }

        beginTest ("parameter overrides reach the engine");
        {
            constexpr int sr = 48000;
            constexpr int seconds = 1;
            juce::AudioBuffer<float> input (1, sr * seconds);
            for (int i = 0; i < input.getNumSamples(); ++i)
                input.setSample (0, i, static_cast<float> (0.3 * std::sin (2.0 * juce::MathConstants<double>::twoPi * 440.0 * i / sr)));

            auto energy = [] (const juce::AudioBuffer<float>& b)
            {
                double s = 0.0;
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        s += static_cast<double> (b.getSample (ch, i)) * b.getSample (ch, i);
                return s;
            };

            offline::RenderOptions flat;
            flat.sampleRate = sr;
            flat.blockSize = 512;

            offline::RenderOptions trimmed;
            trimmed.sampleRate = sr;
            trimmed.blockSize = 512;
            trimmed.paramValues.emplace_back ("output_trim", -12.0f); // -12 dB

            juce::AudioBuffer<float> outFlat, outTrimmed;
            offline::renderBuffer (outFlat, input, flat);
            offline::renderBuffer (outTrimmed, input, trimmed);

            // -12 dB trim -> ~1/4 the power.
            const auto ratio = energy (outTrimmed) / juce::jmax (energy (outFlat), 1e-9);
            expect (std::abs (ratio - 0.0625) < 0.01,
                    juce::String ("trim ratio ") + juce::String (ratio)
                        + " != ~0.0625 (-12 dB)");
        }

        beginTest ("block-size scan renders identically at 1..8192");
        {
            constexpr int sr = 48000;
            constexpr int seconds = 1;
            juce::AudioBuffer<float> input (2, sr * seconds);
            for (int i = 0; i < input.getNumSamples(); ++i)
            {
                const auto t = static_cast<double> (i) / sr;
                input.setSample (0, i, static_cast<float> (0.25 * std::sin (2.0 * juce::MathConstants<double>::twoPi * 220.0 * t)));
                input.setSample (1, i, static_cast<float> (0.25 * std::sin (2.0 * juce::MathConstants<double>::twoPi * 330.0 * t)));
            }

            juce::AudioBuffer<float> reference;
            {
                offline::RenderOptions o;
                o.sampleRate = sr;
                o.blockSize = 512;
                offline::renderBuffer (reference, input, o);
            }

            for (int block : { 1, 7, 64, 8192 })
            {
                offline::RenderOptions o;
                o.sampleRate = sr;
                o.blockSize = block;
                juce::AudioBuffer<float> out;
                offline::renderBuffer (out, input, o);

                double maxDiff = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < out.getNumSamples(); ++i)
                        maxDiff = juce::jmax (maxDiff,
                                              std::abs (static_cast<double> (out.getSample (ch, i))
                                                        - reference.getSample (ch, i)));
                expect (maxDiff < 1e-4,
                        juce::String ("block ") + juce::String (block)
                            + " differs from block 512 by " + juce::String (maxDiff));
            }
        }

        beginTest ("formant latency is compensated (output aligned to input)");
        {
            // With formant_enabled the engine reports 2048 samples of latency;
            // the renderer must shift the output forward so an early impulse
            // lands near sample 0 instead of 2048 (review B).
            constexpr int sr = 48000;
            constexpr int seconds = 1;
            juce::AudioBuffer<float> input (1, sr * seconds);
            input.clear();
            input.setSample (0, 0, 0.9f); // impulse at the very start

            offline::RenderOptions o;
            o.sampleRate = sr;
            o.blockSize = 512;
            o.paramValues.emplace_back ("formant_enabled", 1.0f);

            juce::AudioBuffer<float> output;
            offline::renderBuffer (output, input, o);

            int peakIdx = -1;
            float peak = 0.0f;
            for (int i = 0; i < output.getNumSamples(); ++i)
            {
                const auto v = std::abs (output.getSample (0, i));
                if (v > peak)
                {
                    peak = v;
                    peakIdx = i;
                }
            }
            logMessage (juce::String ("formant impulse peak at ") + juce::String (peakIdx)
                        + " (latency 2048 would land at 2048 without compensation)");
            expect (peakIdx >= 0 && peakIdx < 256,
                    juce::String ("impulse peak at ") + juce::String (peakIdx)
                        + " — latency compensation missing?");
            expect (peak > 0.01f, "impulse response is silent");
        }

        beginTest ("60 s stereo render completes without anomalies");
        {
            constexpr int sr = 48000;
            constexpr int seconds = 60;
            juce::AudioBuffer<float> input (2, sr * seconds);
            juce::Random rng (12345);
            for (int i = 0; i < input.getNumSamples(); ++i)
            {
                // Pink-ish noise floor + a tone, so every stage is exercised.
                const auto t = static_cast<double> (i) / sr;
                input.setSample (0, i, static_cast<float> (0.1 * std::sin (2.0 * juce::MathConstants<double>::twoPi * 120.0 * t)
                                                           + 0.02 * rng.nextFloat()));
                input.setSample (1, i, static_cast<float> (0.1 * std::sin (2.0 * juce::MathConstants<double>::twoPi * 140.0 * t)
                                                           + 0.02 * rng.nextFloat()));
            }

            offline::RenderOptions o;
            o.sampleRate = sr;
            o.blockSize = 512;
            o.paramValues.emplace_back ("b1_jitter_depth", 8.0f);
            o.paramValues.emplace_back ("b2_shimmer_depth", 30.0f);

            juce::AudioBuffer<float> output;
            const auto start = juce::Time::getMillisecondCounterHiRes();
            const auto n = offline::renderBuffer (output, input, o);
            const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;

            expectEquals (n, sr * seconds, "60 s rendered fully");
            bool finite = true;
            double peak = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < output.getNumSamples(); i += 64)
                {
                    const auto v = output.getSample (ch, i);
                    finite = finite && std::isfinite (v);
                    peak = juce::jmax (peak, static_cast<double> (std::abs (v)));
                }
            expect (finite, "60 s render produced non-finite samples");
            expect (peak > 0.01, "60 s render is silent");
            logMessage (juce::String ("60 s render wall time: ") + juce::String (elapsed, 2) + " s");
        }
    }
};

OfflineRenderTests offlineRenderTests;

} // namespace
