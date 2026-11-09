/*
    NeoVoiDrive T58/T59/T60 tests — whole-band phase fog (SPEC §4.3 / DECISIONS R50).

    BF-U01  Rough = 0 is a sample-exact passthrough.
    BF-U02  Rough 30/60 widens the 1 kHz line (line-share falls monotonically).
    BF-U03  192 kHz extreme is finite and keeps the total RMS.
    BF-U04  BandProcessor applies the phase fog after the engine mix (Legacy).
    BF-U05  Same seed keeps two channels sample-identical.
    BF-U06  R30/R50 widening without amplitude modulation (10 ms envelope CV).
    BF-U07  The 100 Hz harmonic widens too (constant-Hz phase fog).
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/BandFogUnit.h"
#include "dsp/BandProcessor.h"

#include <cmath>
#include <vector>

namespace
{

constexpr double testSampleRate = 48000.0;

void fillSine (std::vector<float>& out, double freq, double seconds, float amp = 0.5f,
               double sr = testSampleRate)
{
    out.resize (static_cast<size_t> (sr * seconds));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = amp * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * freq * static_cast<double> (i) / sr));
}

std::vector<float> fftPower (const std::vector<float>& sig, int order = 18)
{
    const int n = 1 << order;
    std::vector<float> data (static_cast<size_t> (2 * n), 0.0f);
    const auto m = juce::jmin (static_cast<int> (sig.size()), n);
    for (int i = 0; i < m; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                            * static_cast<double> (i)
                                            / static_cast<double> (juce::jmax (1, m - 1)));
        data[static_cast<size_t> (i)] = static_cast<float> (sig[static_cast<size_t> (i)] * w);
    }
    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (data.data());

    std::vector<float> power (static_cast<size_t> (n / 2), 0.0f);
    for (size_t i = 0; i < power.size(); ++i)
        power[i] = data[i] * data[i];
    return power;
}

double bandPower (const std::vector<float>& power, double lo, double hi,
                  double sr, int order = 18)
{
    const auto n = static_cast<double> (1 << order);
    const auto binLo = juce::jmax (0, static_cast<int> (std::floor (lo * n / sr)));
    const auto binHi = juce::jmin (static_cast<int> (power.size()) - 1,
                                   static_cast<int> (std::ceil (hi * n / sr)));
    double s = 0.0;
    for (int i = binLo; i <= binHi; ++i)
        s += static_cast<double> (power[static_cast<size_t> (i)]);
    return s;
}

double lineShare (const std::vector<float>& sig, double freq)
{
    const auto p = fftPower (sig);
    const auto total = bandPower (p, 20.0, 16000.0, testSampleRate);
    const auto core = bandPower (p, freq - 5.0, freq + 5.0, testSampleRate);
    return core / juce::jmax (1.0e-30, total);
}

double insideShare (const std::vector<float>& sig, double freq, double halfWidth)
{
    const auto p = fftPower (sig);
    const auto total = bandPower (p, 20.0, 16000.0, testSampleRate);
    const auto inside = bandPower (p, freq - halfWidth, freq + halfWidth, testSampleRate);
    return inside / juce::jmax (1.0e-30, total);
}

double rms (const std::vector<float>& v)
{
    double sum = 0.0;
    for (const auto x : v)
        sum += static_cast<double> (x) * x;
    return std::sqrt (sum / juce::jmax<size_t> (1, v.size()));
}

double db (double v) noexcept { return 20.0 * std::log10 (juce::jmax (1.0e-30, v)); }

double envelopeCv (const std::vector<float>& sig, double startSeconds)
{
    const int win = static_cast<int> (0.010 * testSampleRate);
    const int hop = win / 2;
    const auto start = static_cast<size_t> (startSeconds * testSampleRate);
    double sum = 0.0, sq = 0.0;
    int count = 0;
    for (size_t s = start; s + static_cast<size_t> (win) < sig.size();
         s += static_cast<size_t> (hop))
    {
        double e = 0.0;
        for (int k = 0; k < win; ++k)
        {
            const double v = sig[s + static_cast<size_t> (k)];
            e += v * v;
        }
        const auto r = std::sqrt (e / win);
        sum += r;
        sq += r * r;
        ++count;
    }
    const auto mean = sum / juce::jmax (1, count);
    return std::sqrt (juce::jmax (0.0, sq / juce::jmax (1, count) - mean * mean))
           / juce::jmax (1.0e-30, mean);
}

std::vector<float> renderFog (dsp::BandFogUnit& unit, const std::vector<float>& in,
                              float amount, std::int64_t seed)
{
    unit.prepare (testSampleRate);
    unit.setSeed (seed);
    dsp::BandFogUnit::Params p;
    p.amount = amount;
    unit.setParameters (p);

    std::vector<float> out (in.size(), 0.0f);
    for (size_t i = 0; i < in.size(); ++i)
        out[i] = unit.process (in[i]);
    return out;
}

class BandFogUnitTests final : public juce::UnitTest
{
public:
    BandFogUnitTests() : juce::UnitTest ("BandFog") {}

    void runTest() override
    {
        beginTest ("Rough = 0 is a sample-exact passthrough");
        {
            dsp::BandFogUnit unit;
            std::vector<float> in;
            fillSine (in, 1000.0, 0.5);

            dsp::BandFogUnit::Params p;
            p.amount = 0.0f;
            unit.prepare (testSampleRate);
            unit.setSeed (7);
            unit.setParameters (p);

            float maxDiff = 0.0f;
            for (float x : in)
                maxDiff = juce::jmax (maxDiff, std::abs (unit.process (x) - x));

            expectEquals (maxDiff, 0.0f, "Rough = 0 must be bit-transparent");
        }

        beginTest ("Rough 30/60 widens the 1 kHz line monotonically");
        {
            std::vector<float> in;
            fillSine (in, 1000.0, 2.2);
            const auto analyse = [&] (float amount)
            {
                dsp::BandFogUnit unit;
                const auto out = renderFog (unit, in, amount, 0xBF061);
                std::vector<float> tail (out.begin() + static_cast<long> (0.2 * testSampleRate),
                                         out.end());
                const auto share = lineShare (tail, 1000.0);
                logMessage ("  R" + juce::String (amount, 0) + ": line ±5 "
                            + juce::String (100.0 * share, 2) + " %");
                return share;
            };

            const auto s0 = analyse (0.0f);
            const auto s30 = analyse (30.0f);
            const auto s60 = analyse (60.0f);
            logMessage ("  line share 0/30/60: " + juce::String (100.0 * s0, 2) + " / "
                        + juce::String (100.0 * s30, 2) + " / "
                        + juce::String (100.0 * s60, 2) + " %");

            expectGreaterThan (s0, 0.85, "Rough = 0 should stay a line");
            expectLessThan (s30, s0 - 0.10, "Rough 30 did not widen the line");
            expectLessThan (s60, s30 - 0.10, "Rough 60 should widen more than Rough 30");
        }

        beginTest ("192 kHz extreme is finite and keeps the total RMS");
        {
            dsp::BandFogUnit unit;
            unit.prepare (192000.0);
            unit.setSeed (99);

            dsp::BandFogUnit::Params p;
            p.amount = 100.0f;
            unit.setParameters (p);

            std::vector<float> out (192000);
            bool finite = true;
            float phaseMax = 0.0f;
            for (int i = 0; i < 192000; ++i)
            {
                const auto x = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                * 1000.0f * static_cast<float> (i) / 192000.0f);
                out[static_cast<size_t> (i)] = unit.process (x);
                finite = finite && std::isfinite (out[static_cast<size_t> (i)]);
                phaseMax = juce::jmax (phaseMax, std::abs (unit.getLastPhaseRadians()));
            }

            const std::vector<float> tail (out.begin() + 48000, out.end());
            const auto rmsDb = db (rms (tail) / 0.353553);
            logMessage ("  192 kHz R100: RMS " + juce::String (rmsDb, 2) + " dB, phase max "
                        + juce::String (phaseMax, 3));
            expect (finite, "192 kHz phase fog produced a non-finite sample");
            expectLessThan (static_cast<float> (std::abs (rmsDb)), 1.5f);
            expectGreaterThan (phaseMax, 0.0f);
        }

        beginTest ("BandProcessor applies the phase fog after the engine mix (Legacy engine)");
        {
            const auto renderBand = [] (float fogAmount)
            {
                dsp::BandProcessor band;
                band.prepare (testSampleRate, 512);
                band.setSeed (0x1234);
                band.setEngineVersion (false);   // Legacy: isolatable from Sideband

                params::ParamSnapshot::Band p;
                p.vibWave = 0;
                p.fog = fogAmount;
                band.setParameters (p);

                std::vector<float> out (48000);
                float phaseMax = 0.0f;
                for (int i = 0; i < 48000; ++i)
                {
                    const auto x = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                    * 1000.0f * static_cast<float> (i) / 48000.0f);
                    out[static_cast<size_t> (i)] = band.process (x);
                    phaseMax = juce::jmax (phaseMax, std::abs (band.getBandFogLastPhaseRadians()));
                }
                return std::pair { out, phaseMax };
            };

            const auto [dry, dryPhase] = renderBand (0.0f);
            const auto [fogged, fogPhase] = renderBand (100.0f);

            float maxDiff = 0.0f;
            bool finite = true;
            for (size_t i = 0; i < dry.size(); ++i)
            {
                maxDiff = juce::jmax (maxDiff, std::abs (fogged[i] - dry[i]));
                finite = finite && std::isfinite (fogged[i]);
            }

            expectEquals (dryPhase, 0.0f, "Rough = 0 should leave the phase fog off");
            expectGreaterThan (std::abs (fogPhase), 0.0f, "Rough = 100 should engage the phase fog");
            expectGreaterThan (maxDiff, 1.0e-3f, "phase fog did not reach the Legacy band output");
            expect (finite, "band phase fog produced a non-finite sample");
        }

        beginTest ("same seed keeps two channels sample-identical");
        {
            dsp::BandFogUnit a, b;
            a.prepare (testSampleRate);
            b.prepare (testSampleRate);
            a.setSeed (0x5150);
            b.setSeed (0x5150);

            dsp::BandFogUnit::Params p;
            p.amount = 80.0f;
            a.setParameters (p);
            b.setParameters (p);

            float maxDiff = 0.0f;
            for (int i = 0; i < 48000; ++i)
            {
                const auto x = 0.4f * std::sin (juce::MathConstants<float>::twoPi
                                                * 300.0f * static_cast<float> (i) / 48000.0f);
                maxDiff = juce::jmax (maxDiff, std::abs (a.process (x) - b.process (x)));
            }
            expectEquals (maxDiff, 0.0f, "same seed must keep the two channels identical");
        }

        beginTest ("R30/R50 widen without amplitude modulation");
        {
            std::vector<float> in;
            fillSine (in, 1000.0, 2.2);

            const auto renderAndMeasure = [&] (float amount)
            {
                dsp::BandFogUnit unit;
                const auto out = renderFog (unit, in, amount, 0xBF063);
                std::vector<float> tail (out.begin() + static_cast<long> (0.2 * testSampleRate),
                                         out.end());
                const auto line = lineShare (tail, 1000.0);
                const auto inside100 = insideShare (tail, 1000.0, 100.0);
                const auto cv = envelopeCv (out, 0.2);
                const auto rmsDb = db (rms (tail) / juce::jmax (1e-30, rms (
                    std::vector<float> (in.begin() + static_cast<long> (0.2 * testSampleRate),
                                        in.end()))));
                logMessage ("  R" + juce::String (amount, 0) + ": RMS "
                            + juce::String (rmsDb, 2) + " dB, line ±5 "
                            + juce::String (100.0 * line, 2) + " %, inside ±100 "
                            + juce::String (100.0 * inside100, 2) + " %, env CV "
                            + juce::String (cv, 4));
                return std::tuple { line, inside100, cv, rmsDb };
            };

            const auto [l30, i30, c30, r30] = renderAndMeasure (30.0f);
            const auto [l50, i50, c50, r50] = renderAndMeasure (50.0f);

            expectLessThan (static_cast<float> (std::abs (r30)), 1.5f);
            expectLessThan (static_cast<float> (std::abs (r50)), 1.5f);
            expectLessThan (static_cast<float> (l30), 0.95f);
            expectLessThan (static_cast<float> (l50), 0.40f);
            expectLessThan (static_cast<float> (i50), 0.80f);
            expectLessThan (static_cast<float> (c30), 0.05f);
            expectLessThan (static_cast<float> (c50), 0.05f);
        }

        beginTest ("100 Hz harmonic widens at R30/R50");
        {
            std::vector<float> in;
            fillSine (in, 100.0, 2.2);
            const auto measure = [&] (float amount)
            {
                dsp::BandFogUnit unit;
                const auto out = renderFog (unit, in, amount, 0xBF064);
                std::vector<float> tail (out.begin() + static_cast<long> (0.2 * testSampleRate),
                                         out.end());
                return std::pair { lineShare (tail, 100.0), insideShare (tail, 100.0, 100.0) };
            };

            const auto [l30, i30] = measure (30.0f);
            const auto [l50, i50] = measure (50.0f);
            logMessage ("  100 Hz line ±5 / inside ±100: R30 "
                        + juce::String (100.0 * l30, 1) + " / " + juce::String (100.0 * i30, 1)
                        + " %; R50 " + juce::String (100.0 * l50, 1) + " / "
                        + juce::String (100.0 * i50, 1) + " %");

            expectLessThan (static_cast<float> (l30), 0.95f);
            expectLessThan (static_cast<float> (l50), 0.50f);
            expectLessThan (static_cast<float> (l50), static_cast<float> (l30) - 0.10f);
            expectLessThan (static_cast<float> (i50), 0.95f);
        }
    }
};

BandFogUnitTests bandFogUnitTests;

} // namespace
