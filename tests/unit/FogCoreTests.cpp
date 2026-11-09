/*
    NeoVoiDrive T58/T59/T60 tests — shared FogCore (SPEC §4.3 / DECISIONS R50).

    FC-U01  F scale helper stays compatible with the Sideband PDC reserve.
    FC-U02  IIR Hilbert pair: i/q are unit-magnitude and in quadrature.
    FC-U03  PhaseFog widens the line without adding amplitude modulation;
            T59 phase-rate guard keeps the grain detail-forward.
    FC-U04  Silent input stays exactly silent (no additive noise source).
    FC-U05  Seeded phase control is deterministic and finite.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/FogCore.h"

#include <cmath>
#include <vector>

namespace
{

std::vector<float> fftPower (const std::vector<float>& sig, int order = 18)
{
    const int n = 1 << order;
    std::vector<float> data (static_cast<size_t> (2 * n), 0.0f);
    const auto count = juce::jmin (static_cast<int> (sig.size()), n);
    for (int i = 0; i < count; ++i)
        data[static_cast<size_t> (i)] = sig[static_cast<size_t> (i)];

    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (data.data());

    std::vector<float> power (static_cast<size_t> (n / 2 + 1), 0.0f);
    for (size_t i = 0; i < power.size(); ++i)
        power[i] = data[i] * data[i];
    return power;
}

double bandPower (const std::vector<float>& power, double lo, double hi,
                  double sr, int order = 18)
{
    const double n = static_cast<double> (1 << order);
    const auto binLo = juce::jmax (0, static_cast<int> (std::floor (lo * n / sr)));
    const auto binHi = juce::jmin (static_cast<int> (power.size()) - 1,
                                   static_cast<int> (std::ceil (hi * n / sr)));
    double sum = 0.0;
    for (int i = binLo; i <= binHi; ++i)
        sum += static_cast<double> (power[static_cast<size_t> (i)]);
    return sum;
}

double rms (const std::vector<float>& v)
{
    double sum = 0.0;
    for (const auto x : v)
        sum += static_cast<double> (x) * x;
    return std::sqrt (sum / juce::jmax<size_t> (1, v.size()));
}

class FogCoreTests final : public juce::UnitTest
{
public:
    FogCoreTests() : juce::UnitTest ("FogCore") {}

    void runTest() override
    {
        beginTest ("F scale still matches the Sideband PDC reserve");
        {
            expectWithinAbsoluteError (dsp::FogCore::fogSamplesForRate (44100.0), 31.0f, 1.0e-3f);
            expectWithinAbsoluteError (dsp::FogCore::fogSamplesForRate (48000.0), 34.0f, 1.0e-3f);
            expectWithinAbsoluteError (dsp::FogCore::fogSamplesForRate (96000.0), 68.0f, 1.0e-3f);
            expectWithinAbsoluteError (dsp::FogCore::fogSamplesForRate (192000.0), 136.0f, 1.0e-3f);
        }

        beginTest ("IIR Hilbert pair is unit-magnitude and in quadrature");
        {
            dsp::FogCore::Hilbert h;
            h.prepare (48000.0);

            constexpr double sr = 48000.0;
            constexpr double freq = 1000.0;
            constexpr int warmup = 4800;
            constexpr int measure = 48000;

            double sumI = 0.0, sumQ = 0.0, sumIQ = 0.0;
            std::vector<float> is, qs;
            is.reserve (measure);
            qs.reserve (measure);
            for (int n = 0; n < warmup + measure; ++n)
            {
                const float x = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                 * static_cast<float> (freq) * n
                                                 / static_cast<float> (sr));
                float i = 0.0f, q = 0.0f;
                h.process (x, i, q);
                if (n >= warmup)
                {
                    is.push_back (i);
                    qs.push_back (q);
                    sumI += static_cast<double> (i) * i;
                    sumQ += static_cast<double> (q) * q;
                    sumIQ += static_cast<double> (i) * q;
                }
            }

            const auto rmsI = std::sqrt (sumI / measure);
            const auto rmsQ = std::sqrt (sumQ / measure);
            const auto corr = sumIQ / std::sqrt (juce::jmax (1.0e-30, sumI * sumQ));

            double sumIQPrev = 0.0, sumIQNext = 0.0;
            for (size_t n = 1; n + 1 < is.size(); ++n)
            {
                sumIQPrev += static_cast<double> (is[n]) * qs[n - 1];
                sumIQNext += static_cast<double> (is[n]) * qs[n + 1];
            }
            const auto corrPrev = sumIQPrev / std::sqrt (juce::jmax (1.0e-30, sumI * sumQ));
            const auto corrNext = sumIQNext / std::sqrt (juce::jmax (1.0e-30, sumI * sumQ));
            logMessage ("  Hilbert 1 kHz: RMS i/q "
                        + juce::String (rmsI, 4) + " / " + juce::String (rmsQ, 4)
                        + ", iq corr " + juce::String (corr, 4)
                        + " (q-1 " + juce::String (corrPrev, 4)
                        + ", q+1 " + juce::String (corrNext, 4) + ")");
            expectWithinAbsoluteError (static_cast<float> (rmsI), 0.353553f, 0.005f);
            expectWithinAbsoluteError (static_cast<float> (rmsQ), 0.353553f, 0.005f);
            // This is the alignment used by the approved fineC prototype;
            // the small residual (~7 deg at 1 kHz) is part of that texture.
            // The PhaseFog test below guards the audible AM/widening result.
            expectLessThan (static_cast<float> (std::abs (corr)), 0.15f);
        }

        beginTest ("PhaseFog widens the line without amplitude modulation");
        {
            dsp::FogCore::PhaseFog fog;
            fog.prepare (48000.0);
            fog.setSeed (0x5F0C1234LL);

            constexpr double sr = 48000.0;
            constexpr int n = static_cast<int> (2.5 * sr);
            constexpr double freq = 1000.0;
            std::vector<float> in (static_cast<size_t> (n), 0.0f);
            std::vector<float> out (static_cast<size_t> (n), 0.0f);
            std::vector<float> phaseTrace (static_cast<size_t> (n), 0.0f);
            for (int k = 0; k < n; ++k)
            {
                in[static_cast<size_t> (k)] = 0.5f * std::sin (
                    juce::MathConstants<float>::twoPi * static_cast<float> (freq)
                    * k / static_cast<float> (sr));
                out[static_cast<size_t> (k)] = fog.process (in[static_cast<size_t> (k)], 0.5f);
                phaseTrace[static_cast<size_t> (k)] = fog.getLastPhase();
            }

            const auto tailStart = static_cast<size_t> (0.25 * sr);
            const std::vector<float> inTail (in.begin() + static_cast<long> (tailStart), in.end());
            const std::vector<float> outTail (out.begin() + static_cast<long> (tailStart), out.end());

            const auto p = fftPower (outTail);
            const auto total = bandPower (p, 20.0, 16000.0, sr);
            const auto core5 = bandPower (p, freq - 5.0, freq + 5.0, sr);
            const auto core100 = bandPower (p, freq - 100.0, freq + 100.0, sr);
            const auto coreShare = core5 / juce::jmax (1.0e-30, total);
            const auto inside100 = core100 / juce::jmax (1.0e-30, total);

            // Short-term RMS must stay constant: phase rotation only.
            const int win = static_cast<int> (0.010 * sr);
            const int hop = win / 2;
            double envSum = 0.0, envSq = 0.0;
            int envCount = 0;
            for (size_t s = tailStart; s + static_cast<size_t> (win) < out.size();
                 s += static_cast<size_t> (hop))
            {
                double e = 0.0;
                for (int k = 0; k < win; ++k)
                {
                    const double v = out[s + static_cast<size_t> (k)];
                    e += v * v;
                }
                const auto r = std::sqrt (e / win);
                envSum += r;
                envSq += r * r;
                ++envCount;
            }
            const auto envMean = envSum / juce::jmax (1, envCount);
            const auto envCv = std::sqrt (juce::jmax (0.0, envSq / juce::jmax (1, envCount)
                                                           - envMean * envMean))
                               / juce::jmax (1.0e-30, envMean);

            const auto rmsDb = 20.0 * std::log10 (rms (outTail) / juce::jmax (1e-30, rms (inTail)));
            logMessage ("  PhaseFog R50 1 kHz: core ±5 " + juce::String (100.0 * coreShare, 2)
                        + " %, inside ±100 " + juce::String (100.0 * inside100, 2)
                        + " %, RMS " + juce::String (rmsDb, 2)
                        + " dB, 10 ms envelope CV " + juce::String (envCv, 4));

            // T60: the phase-rate power below the 300 Hz roughness band is
            // what reads as large grains; too much detail (T59) instead puts
            // a far skirt under the harmonics. 0.70/0.30 keeps the rate above
            // T58 while cutting T59's far fog.
            std::vector<float> phaseRate;
            phaseRate.reserve (static_cast<size_t> (n));
            for (size_t k = tailStart + 1; k < phaseTrace.size(); ++k)
                phaseRate.push_back ((phaseTrace[k] - phaseTrace[k - 1])
                                     * static_cast<float> (sr)
                                     / juce::MathConstants<float>::twoPi);

            const auto phaseCount = phaseRate.size();
            const auto phaseRateRms = rms (phaseRate);
            for (size_t k = 0; k < phaseCount; ++k)
                phaseRate[k] *= 0.5f - 0.5f * std::cos (
                    juce::MathConstants<float>::twoPi * static_cast<float> (k)
                    / static_cast<float> (juce::jmax<size_t> (1, phaseCount - 1)));

            const auto phasePower = fftPower (phaseRate, 17);
            const auto slowShare = bandPower (phasePower, 10.0, 300.0, sr, 17)
                                 / juce::jmax (1.0e-30, bandPower (phasePower, 10.0, 8000.0, sr, 17));
            logMessage ("  phase-rate RMS " + juce::String (phaseRateRms, 1)
                        + " Hz, <300 Hz share "
                        + juce::String (100.0 * slowShare, 2) + " %");

            expectLessThan (static_cast<float> (std::abs (rmsDb)), 1.5f);
            expectLessThan (static_cast<float> (coreShare), 0.40f);
            expectLessThan (static_cast<float> (inside100), 0.80f);
            expectLessThan (static_cast<float> (envCv), 0.05f);
            // T60: the old 0.75/0.25 split measured ~459 Hz phase rate and
            // ~3.3 % below 300 Hz; 0.70/0.30 must stay faster/finer without
            // T59's far fog layer.
            expectGreaterThan (phaseRateRms, 520.0,
                               "phase control became too slow (coarse grain, T60)");
            expectLessThan (static_cast<float> (slowShare), 0.028f,
                            "phase control reverted to a slow, coarse-grain split (T60)");
        }

        beginTest ("PhaseFog adds no audio noise on a silent input");
        {
            dsp::FogCore::PhaseFog fog;
            fog.prepare (48000.0);
            fog.setSeed (0x5F0C5678LL);

            float peak = 0.0f;
            for (int i = 0; i < 48000; ++i)
                peak = juce::jmax (peak, std::abs (fog.process (0.0f, 1.0f)));
            expectLessThan (peak, 1.0e-9f, "phase fog must not add an audio noise layer");
        }

        beginTest ("seeded phase control is deterministic and finite");
        {
            dsp::FogCore::PhaseFog a, b, c;
            a.prepare (48000.0); b.prepare (48000.0); c.prepare (48000.0);
            a.setSeed (0x12345678LL);
            b.setSeed (0x12345678LL);
            c.setSeed (0x12345679LL);

            float maxDiff = 0.0f, maxAbs = 0.0f, sumA = 0.0f, sumC = 0.0f;
            bool finite = true;
            for (int n = 0; n < 24000; ++n)
            {
                const float x = 0.3f * std::sin (juce::MathConstants<float>::twoPi
                                                 * 220.0f * n / 48000.0f)
                              + 0.12f * std::sin (juce::MathConstants<float>::twoPi
                                                  * 1333.0f * n / 48000.0f);
                const auto ya = a.process (x, 0.8f);
                const auto yb = b.process (x, 0.8f);
                const auto yc = c.process (x, 0.8f);
                maxDiff = juce::jmax (maxDiff, std::abs (ya - yb));
                maxAbs = juce::jmax (maxAbs, std::abs (ya));
                sumA += ya;
                sumC += yc;
                finite = finite && std::isfinite (ya) && std::isfinite (yc);
            }

            expectEquals (maxDiff, 0.0f, "same seed must produce identical phase fog");
            expectLessThan (maxAbs, 2.0f, "phase-fog output must stay bounded");
            expectGreaterThan (std::abs (sumA - sumC), 1.0f,
                               "different seeds should produce different phase fog");
            expect (finite, "phase-fog output must be finite");
        }
    }
};

FogCoreTests fogCoreTests;

} // namespace
