/*
    NeoVoiDrive T66 tests — global Growl engine (SPEC §4.2, DECISIONS R54).

    GR-U01  amount=0 is a sample-exact passthrough, including after EQ state.
    GR-U02  unvoiced/low-clarity input stays sample-exact and gated off.
    GR-U03  voiced AM gain is bounded and the main rate follows rate×F0.
    GR-U04  the 2× component is present in the gain spectrum.
    GR-U05  tone 0/+/−100 produce the N3 −3/−6/0 dB response at 4 kHz.
    GR-U06  mono/stereo identity and seed determinism.
    GR-U07  NaN/∞ parameters are clamped and never poison the output.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/GrowlEngine.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace
{

constexpr double testSampleRate = 48000.0;
constexpr int blockSize = 512;

std::vector<float> makeSine (double freq, double seconds, float amp = 0.3f)
{
    const auto n = static_cast<size_t> (testSampleRate * seconds);
    std::vector<float> out (n);
    for (size_t i = 0; i < n; ++i)
        out[i] = amp * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * freq * static_cast<double> (i) / testSampleRate));
    return out;
}

double rms (const std::vector<float>& v)
{
    double s = 0.0;
    for (auto x : v) s += static_cast<double> (x) * x;
    return std::sqrt (s / juce::jmax<size_t> (1, v.size()));
}

double db (double v) noexcept { return 20.0 * std::log10 (juce::jmax (1.0e-30, v)); }

/** Runs the engine over a mono signal, calling setParameters at block edges. */
std::vector<float> renderMono (dsp::GrowlEngine& engine,
                               const std::vector<float>& input,
                               dsp::GrowlEngine::Params params,
                               float f0 = 440.0f,
                               float clarity = 1.0f,
                               bool voiced = true)
{
    engine.setParameters (params);
    std::vector<float> out (input.size(), 0.0f);

    for (size_t i = 0; i < input.size(); ++i)
    {
        if (i % static_cast<size_t> (blockSize) == 0)
            engine.setParameters (params);

        const float g = engine.nextGain (f0, clarity, voiced);
        out[i] = engine.process (input[i], 0, g);
    }
    return out;
}

double dominantFreq (const std::vector<float>& signal, double lo, double hi)
{
    constexpr int order = 17;
    constexpr int n = 1 << order;
    std::vector<float> data (static_cast<size_t> (2 * n), 0.0f);
    const auto m = juce::jmin (static_cast<int> (signal.size()), n);
    for (int i = 0; i < m; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                            * static_cast<double> (i)
                                            / static_cast<double> (juce::jmax (1, m - 1)));
        data[static_cast<size_t> (i)] = static_cast<float> (signal[static_cast<size_t> (i)] * w);
    }

    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (data.data());

    const double binHz = testSampleRate / static_cast<double> (n);
    const int b0 = juce::jmax (1, static_cast<int> (std::floor (lo / binHz)));
    const int b1 = juce::jmin (n / 2 - 1, static_cast<int> (std::ceil (hi / binHz)));

    int best = b0;
    for (int i = b0 + 1; i <= b1; ++i)
        if (data[static_cast<size_t> (i)] > data[static_cast<size_t> (best)])
            best = i;

    return best * binHz;
}

class GrowlEngineTests final : public juce::UnitTest
{
public:
    GrowlEngineTests() : juce::UnitTest ("GrowlEngine") {}

    void runTest() override
    {
        const auto input = makeSine (440.0, 0.5, 0.4f);

        beginTest ("amount=0 is sample-exact");
        {
            dsp::GrowlEngine engine;
            engine.prepare (testSampleRate);
            engine.setSeed (1234);

            // Prime the EQ with a non-zero run, then let the 20 ms amount ramp
            // settle to zero before checking the steady amount=0 segment.
            auto warm = renderMono (engine, input, { 100.0f, 0.20f, 0.0f });
            juce::ignoreUnused (warm);
            renderMono (engine, makeSine (440.0, 0.1, 0.4f), { 0.0f, 0.20f, 0.0f });

            const auto out = renderMono (engine, input, { 0.0f, 0.20f, 0.0f });
            bool exact = true;
            for (size_t i = 0; i < input.size(); ++i)
                exact = exact && (out[i] == input[i]);

            expect (exact, "amount=0 must be a bit-exact passthrough");
        }

        beginTest ("P2-5 idle amount skips the control path but keeps the gate alive");
        {
            dsp::GrowlEngine engine;
            engine.prepare (testSampleRate);
            engine.setSeed (99);

            // Amount = 0, voiced: the gate must still open so a later Amount
            // rise attacks normally, while the gain stays exactly 1 (the
            // oscillator/noise path is skipped).
            bool allUnity = true;
            for (int i = 0; i < 4800; ++i) // 100 ms
                allUnity = allUnity && (engine.nextGain (440.0f, 1.0f, true) == 1.0f);

            expect (allUnity, "idle amount must return unity gain");
            expectGreaterThan (engine.getGate(), 0.9f,
                               "the gate must still follow voiced input while idle");
            expectEquals (engine.getEffectiveAmount(), 0.0f);

            // A later Amount rise must produce normal modulation.
            float minG = 10.0f, maxG = -10.0f;
            for (int i = 0; i < 9600; ++i) // 200 ms
            {
                if (i % blockSize == 0)
                    engine.setParameters ({ 100.0f, 0.20f, 0.0f });
                const auto g = engine.nextGain (440.0f, 1.0f, true);
                minG = juce::jmin (minG, g);
                maxG = juce::jmax (maxG, g);
            }

            expectLessThan (minG, 0.95f, "no AM after the idle period");
            expectGreaterThan (maxG, 1.0f, "no AM peak after the idle period");
        }

        beginTest ("unvoiced input is gated to passthrough");
        {
            dsp::GrowlEngine engine;
            engine.prepare (testSampleRate);
            engine.setSeed (77);

            const auto out = renderMono (engine, input, { 100.0f, 0.20f, 0.0f },
                                         440.0f, 0.0f, false);
            bool exact = true;
            for (size_t i = 0; i < input.size(); ++i)
                exact = exact && (out[i] == input[i]);

            expect (exact, "unvoiced material must stay untouched");
            expectWithinAbsoluteError (engine.getGate(), 0.0f, 1.0e-4f,
                                       "gate must stay closed");
        }

        beginTest ("voiced AM gain is bounded and rate follows F0");
        {
            dsp::GrowlEngine engine;
            engine.prepare (testSampleRate);
            engine.setSeed (555);

            const auto warm = makeSine (440.0, 0.25, 0.25f);
            renderMono (engine, warm, { 100.0f, 0.20f, 0.0f });

            const auto tone = makeSine (440.0, 2.0, 0.25f);
            auto params = dsp::GrowlEngine::Params { 100.0f, 0.20f, 0.0f };
            std::vector<float> gains;
            gains.reserve (tone.size());
            for (size_t i = 0; i < tone.size(); ++i)
            {
                if (i % static_cast<size_t> (blockSize) == 0)
                    engine.setParameters (params);
                const float g = engine.nextGain (440.0f, 1.0f, true);
                gains.push_back (g);
                engine.process (tone[i], 0, g);
            }

            float mn = 10.0f, mx = -10.0f;
            for (auto g : gains) { mn = juce::jmin (mn, g); mx = juce::jmax (mx, g); }

            expectGreaterThan (mx, 0.95f, "AM peak should be near unity");
            expectLessThan (mx, 1.30f, "AM peak must stay bounded");
            expectLessThan (mn, 0.70f, "AM must dip");
            expectGreaterThan (mn, 0.20f, "AM dip must stay bounded");

            std::vector<float> ac (gains.size());
            for (size_t i = 0; i < gains.size(); ++i)
                ac[i] = gains[i] - 1.0f;

            const double peakHz = dominantFreq (ac, 50.0, 140.0);
            expectWithinAbsoluteError (peakHz, 88.0, 10.0,
                                       "main AM rate should be 0.20 × 440 Hz");
            expectWithinAbsoluteError (engine.getAmRate(), 88.0f, 12.0f,
                                       "instantaneous AM rate should track 0.20 × F0");
        }

        beginTest ("2x component is present");
        {
            dsp::GrowlEngine engine;
            engine.prepare (testSampleRate);
            engine.setSeed (9001);

            const auto warm = makeSine (440.0, 0.25, 0.25f);
            renderMono (engine, warm, { 100.0f, 0.20f, 0.0f });

            const auto tone = makeSine (440.0, 2.0, 0.25f);
            auto params = dsp::GrowlEngine::Params { 100.0f, 0.20f, 0.0f };
            std::vector<float> ac;
            ac.reserve (tone.size());
            for (size_t i = 0; i < tone.size(); ++i)
            {
                if (i % static_cast<size_t> (blockSize) == 0)
                    engine.setParameters (params);
                ac.push_back (engine.nextGain (440.0f, 1.0f, true) - 1.0f);
            }

            const double f2 = dominantFreq (ac, 150.0, 210.0);
            expectWithinAbsoluteError (f2, 176.0, 16.0,
                                       "second-order component should sit near 2 × 88 Hz");
        }

        beginTest ("tone EQ follows the N3 response at 4 kHz");
        {
            const auto tone = makeSine (4000.0, 1.2, 0.2f);
            const auto paramsFor = [] (float toneValue)
            {
                return dsp::GrowlEngine::Params { 100.0f, 0.20f, toneValue };
            };

            auto renderTone = [&] (float toneValue)
            {
                dsp::GrowlEngine engine;
                engine.prepare (testSampleRate);
                engine.setSeed (4242);
                auto out = renderMono (engine, tone, paramsFor (toneValue));
                const auto start = static_cast<size_t> (0.5 * testSampleRate);
                std::vector<float> steady (out.begin() + static_cast<long> (start), out.end());
                return rms (steady);
            };

            const auto flat = renderTone (-100.0f);
            const auto n3   = renderTone (0.0f);
            const auto dark = renderTone (100.0f);

            expectWithinAbsoluteError (db (n3 / flat), -3.0, 1.2,
                                       "tone 0 should cut 4 kHz by about 3 dB");
            expectWithinAbsoluteError (db (dark / flat), -6.0, 1.2,
                                       "tone +100 should cut 4 kHz by about 6 dB");
            expectGreaterThan (flat, 1.0e-4, "flat EQ render must not be silent");
        }

        beginTest ("stereo identity and seed determinism");
        {
            const auto mono = makeSine (330.0, 0.8, 0.2f);

            auto render = [&] (std::int64_t seed, bool bothChannels)
            {
                dsp::GrowlEngine engine;
                engine.prepare (testSampleRate);
                engine.setSeed (seed);
                engine.setParameters ({ 100.0f, 0.20f, 0.0f });
                std::vector<float> l (mono.size()), r (mono.size());
                for (size_t i = 0; i < mono.size(); ++i)
                {
                    if (i % static_cast<size_t> (blockSize) == 0)
                        engine.setParameters ({ 100.0f, 0.20f, 0.0f });
                    const float g = engine.nextGain (330.0f, 1.0f, true);
                    l[i] = engine.process (mono[i], 0, g);
                    r[i] = bothChannels ? engine.process (mono[i], 1, g) : l[i];
                }
                return std::make_pair (l, r);
            };

            const auto a = render (777, true);
            expect (a.first == a.second, "same input on both channels must be identical");

            const auto b = render (777, false);
            expect (a.first == b.first, "same seed must be deterministic");

            const auto c = render (778, false);
            bool differs = false;
            for (size_t i = 0; i < c.first.size(); ++i)
                differs = differs || std::abs (c.first[i] - a.first[i]) > 1.0e-7f;
            expect (differs, "different seeds should produce different noise/jitter");
        }

        beginTest ("NaN and infinity parameters are clamped safely");
        {
            dsp::GrowlEngine engine;
            engine.prepare (testSampleRate);
            engine.setSeed (1);
            engine.setParameters ({ std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::infinity(),
                                    -std::numeric_limits<float>::infinity() });

            const auto out = renderMono (engine, input,
                                         { std::numeric_limits<float>::quiet_NaN(),
                                           std::numeric_limits<float>::infinity(),
                                           -std::numeric_limits<float>::infinity() });

            bool finite = true;
            for (auto x : out) finite = finite && std::isfinite (x);
            expect (finite, "NaN/inf parameters must not poison the output");
            expect (std::isfinite (engine.getLastGain()), "gain must stay finite");
        }
    }
};

GrowlEngineTests growlEngineTests;

} // namespace
