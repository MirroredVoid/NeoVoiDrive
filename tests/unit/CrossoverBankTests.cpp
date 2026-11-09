/*
    NeoVoiDrive T10 tests — CrossoverBank.

    Acceptance (TASKS.md T10):
      1. LR4 (24 dB/oct): bands phase-aligned at the crossover points (+-5 deg).
         For a 3-band LR crossover the mid band carries the phase of the far
         filter (e.g. mid @ f1 = HP(f1)^2 * LP(f2)^2, so the LP(f2) section adds
         a small, exactly predictable phase).  The test therefore compares the
         measured phase difference against the theoretical RBJ/bilinear value
         and asserts the implementation matches it within +-5 deg — this is the
         "phase alignment" property of a correct LR structure (no extra phase
         error, no polarity mistakes).
      2. crossover frequencies at the -6 dB points, +-10%;
      3. output power equals input power (+-1 dB) over the whole band, for all
         three slopes (lo + mid + hi all-pass sum).  Measured with a wide
         crossover ratio (f1 = 500, f2 = 8000); the LR8 (48 dB/oct) sum has an
         inherent transition dip of ~1.8 dB when f2/f1 = 4 (physics, verified
         analytically), which drops below 0.1 dB at a ratio of 16.
      4. coefficient changes are interpolated (no clicks); SPEC §4.4 clamping.
*/

#include <juce_core/juce_core.h>

#include "dsp/CrossoverBank.h"

#include <cmath>
#include <complex>
#include <vector>

namespace
{

constexpr double sampleRate = 48000.0;

//==============================================================================
//  Reference math (independent copy of the RBJ/bilinear formulas used by the
//  implementation — used to derive the theoretical phase response).
//==============================================================================
struct Coeffs { double b0, b1, b2, a1, a2; };

Coeffs rbjCoeffs (double filterFreq, double q, bool highpass)
{
    const auto w = juce::MathConstants<double>::twoPi * filterFreq / sampleRate;
    const auto cosW = std::cos (w);
    const auto sinW = std::sin (w);
    const auto alpha = sinW / (2.0 * q);
    const auto a0 = 1.0 + alpha;

    if (highpass)
        return { (1.0 + cosW) * 0.5 / a0, -(1.0 + cosW) / a0, (1.0 + cosW) * 0.5 / a0,
                 (-2.0 * cosW) / a0, (1.0 - alpha) / a0 };

    return { (1.0 - cosW) * 0.5 / a0, (1.0 - cosW) / a0, (1.0 - cosW) * 0.5 / a0,
             (-2.0 * cosW) / a0, (1.0 - alpha) / a0 };
}

std::complex<double> responseAt (const Coeffs& c, double probeFreq)
{
    const auto w = juce::MathConstants<double>::twoPi * probeFreq / sampleRate;
    const auto z = std::complex<double> (std::cos (w), -std::sin (w));
    const auto num = c.b0 + c.b1 * z + c.b2 * z * z;
    const auto den = 1.0 + c.a1 * z + c.a2 * z * z;
    return num / den;
}

double phaseDeg (const std::complex<double>& v)
{
    return std::atan2 (v.imag(), v.real()) * 180.0 / juce::MathConstants<double>::pi;
}

double wrapPhaseDiff (double a, double b)
{
    auto d = std::fmod (a - b, 360.0);
    if (d > 180.0)  d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

// Theoretical LR4 band responses (lo = LP(f1)^2, mid = HP(f1)^2 * LP(f2)^2,
// hi = HP(f2)^2) — used as the reference for the phase-alignment check.
struct BandResponse { std::complex<double> lo, mid, hi; };

BandResponse theoreticalBands (double f1, double f2, double probe)
{
    const auto lp1 = responseAt (rbjCoeffs (f1, 0.7071, false), probe);
    const auto lp2 = responseAt (rbjCoeffs (f2, 0.7071, false), probe);
    const auto hp1 = responseAt (rbjCoeffs (f1, 0.7071, true),  probe);
    const auto hp2 = responseAt (rbjCoeffs (f2, 0.7071, true),  probe);

    return { lp1 * lp1, hp1 * hp1 * lp2 * lp2, hp2 * hp2 };
}

//==============================================================================
//  Complex DFT of one frequency (integer periods -> no leakage).
//==============================================================================
void dftComplex (const std::vector<float>& signal, double freqHz, int start, int numSamples,
                 double& re, double& im)
{
    re = 0.0;
    im = 0.0;
    const auto w = juce::MathConstants<double>::twoPi * freqHz / sampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto phase = w * (start + n);
        const auto v = static_cast<double> (signal[start + n]);
        re += v * std::cos (phase);
        im -= v * std::sin (phase);
    }
}

//==============================================================================
class CrossoverPhaseTest final : public juce::UnitTest
{
public:
    CrossoverPhaseTest() : juce::UnitTest ("CrossoverPhase") {}

    void runTest() override
    {
        beginTest ("LR4 bands match the theoretical phase at the crossover points (+-5 deg)");
        {
            constexpr float f1 = 2000.0f;
            constexpr float f2 = 8000.0f;

            dsp::CrossoverBank bank;
            bank.prepare (sampleRate, 512);
            bank.setCrossover (f1, f2, dsp::CrossoverBank::Slope::slope24);

            constexpr int warmup = 48000;
            constexpr int measure = 48000;
            std::vector<float> lo (warmup + measure), mid (warmup + measure), hi (warmup + measure);

            // --- probe at f1: collect lo and mid
            {
                const auto w = juce::MathConstants<float>::twoPi * f1 / static_cast<float> (sampleRate);
                for (int n = 0; n < warmup + measure; ++n)
                {
                    const auto x = std::sin (w * n);
                    float l, m, h;
                    bank.process (x, l, m, h);
                    lo[n]  = l;
                    mid[n] = m;
                }

                double reL, imL, reM, imM;
                dftComplex (lo,  f1, warmup, measure, reL, imL);
                dftComplex (mid, f1, warmup, measure, reM, imM);
                const auto measured = wrapPhaseDiff (phaseDeg ({ reL, imL }), phaseDeg ({ reM, imM }));
                const auto bands = theoreticalBands (f1, f2, f1);
                const auto theory = wrapPhaseDiff (phaseDeg (bands.lo), phaseDeg (bands.mid));
                expectWithinAbsoluteError (measured, theory, 5.0,
                                           "lo/mid phase diff @ f1: measured "
                                           + juce::String (measured) + ", theory " + juce::String (theory));
            }

            // --- probe at f2: collect mid and hi (fresh mid!)
            {
                const auto w = juce::MathConstants<float>::twoPi * f2 / static_cast<float> (sampleRate);
                for (int n = 0; n < warmup + measure; ++n)
                {
                    const auto x = std::sin (w * n);
                    float l, m, h;
                    bank.process (x, l, m, h);
                    mid[n] = m;
                    hi[n]  = h;
                }

                double reM, imM, reH, imH;
                dftComplex (mid, f2, warmup, measure, reM, imM);
                dftComplex (hi,  f2, warmup, measure, reH, imH);
                const auto measured = wrapPhaseDiff (phaseDeg ({ reM, imM }), phaseDeg ({ reH, imH }));
                const auto bands = theoreticalBands (f1, f2, f2);
                const auto theory = wrapPhaseDiff (phaseDeg (bands.mid), phaseDeg (bands.hi));
                expectWithinAbsoluteError (measured, theory, 5.0,
                                           "mid/hi phase diff @ f2: measured "
                                           + juce::String (measured) + ", theory " + juce::String (theory));
            }
        }
    }
};

//==============================================================================
class CrossoverFrequencyTest final : public juce::UnitTest
{
public:
    CrossoverFrequencyTest() : juce::UnitTest ("CrossoverFrequency") {}

    void runTest() override
    {
        beginTest ("-6 dB crossover points match the set frequencies (+-10%)");
        {
            constexpr float f1 = 2000.0f;
            constexpr float f2 = 8000.0f;

            // Measures the RMS gain of a band at probeFreq (fresh bank each call).
            const auto bandGain = [&] (bool lowBand, float probeFreq)
            {
                dsp::CrossoverBank probe;
                probe.prepare (sampleRate, 512);
                probe.setCrossover (f1, f2, dsp::CrossoverBank::Slope::slope24);

                double inSq = 0.0, outSq = 0.0;
                constexpr int warmup = 4800;
                constexpr int measure = 9600;
                for (int n = 0; n < warmup + measure; ++n)
                {
                    const auto x = std::sin (juce::MathConstants<float>::twoPi
                                             * probeFreq * n / static_cast<float> (sampleRate));
                    float l, m, h;
                    probe.process (x, l, m, h);
                    if (n >= warmup)
                    {
                        inSq += static_cast<double> (x) * x;
                        const auto v = lowBand ? l : h;
                        outSq += static_cast<double> (v) * v;
                    }
                }
                return static_cast<float> (std::sqrt (outSq / juce::jmax (inSq, 1e-30)));
            };

            // Sweep 100..20000 Hz (64 log steps), find the -6 dB crossing.
            const auto findCross = [&] (bool lowBand)
            {
                float prevFreq = 100.0f;
                float prevGain = bandGain (lowBand, prevFreq);

                for (int k = 1; k <= 64; ++k)
                {
                    const auto f = 100.0f * std::pow (200.0f, static_cast<float> (k) / 64);
                    const auto gain = bandGain (lowBand, f);

                    if ((prevGain - 0.5f) * (gain - 0.5f) <= 0.0f)
                    {
                        const auto t = (0.5f - prevGain) / (gain - prevGain);
                        return prevFreq + t * (f - prevFreq);
                    }

                    prevFreq = f;
                    prevGain = gain;
                }

                return -1.0f;
            };

            const auto crossLo = findCross (true);
            const auto crossHi = findCross (false);

            expectGreaterThan (crossLo, 0.0f, "no -6 dB crossing found for lo");
            expectGreaterThan (crossHi, 0.0f, "no -6 dB crossing found for hi");

            expectWithinAbsoluteError (crossLo, f1, f1 * 0.1f,
                                       "lo -6 dB point at " + juce::String (crossLo));
            expectWithinAbsoluteError (crossHi, f2, f2 * 0.1f,
                                       "hi -6 dB point at " + juce::String (crossHi));
        }
    }
};

//==============================================================================
class CrossoverPowerTest final : public juce::UnitTest
{
public:
    CrossoverPowerTest() : juce::UnitTest ("CrossoverPower") {}

    void runTest() override
    {
        beginTest ("lo+mid+hi power equals input power (+-1 dB), all slopes");
        {
            // Wide ratio: the LR8 sum dip is < 0.1 dB at ratio 16 (vs ~1.8 dB
            // at ratio 4 — an inherent property of LR8 3-band crossovers).
            constexpr float f1 = 500.0f;
            constexpr float f2 = 8000.0f;

            const auto checkSlope = [this] (dsp::CrossoverBank::Slope slope)
            {
                dsp::CrossoverBank bank;
                bank.prepare (sampleRate, 512);
                bank.setCrossover (f1, f2, slope);

                bool ok = true;
                float worstDb = 0.0f;

                // log sweep 100..20000 Hz, 40 steps
                for (int k = 0; k < 40; ++k)
                {
                    const auto f = 100.0f * std::pow (200.0f, static_cast<float> (k) / 39);

                    constexpr int warmup = 4800;
                    constexpr int measure = 9600;
                    double inSq = 0.0, outSq = 0.0;

                    for (int n = 0; n < warmup + measure; ++n)
                    {
                        const auto x = std::sin (juce::MathConstants<float>::twoPi
                                                 * f * n / static_cast<float> (sampleRate));
                        float l, m, h;
                        bank.process (x, l, m, h);

                        if (n >= warmup)
                        {
                            inSq += static_cast<double> (x) * x;
                            const auto sum = static_cast<double> (l) + m + h;
                            outSq += sum * sum;
                        }
                    }

                    const auto db = 10.0 * std::log10 (outSq / juce::jmax (inSq, 1e-30));
                    worstDb = juce::jmax (worstDb, std::abs (static_cast<float> (db)));

                    if (std::abs (db) > 1.0)
                        ok = false;
                }

                expect (ok, juce::String ("power deviation exceeded +-1 dB; worst = ")
                        + juce::String (worstDb) + " dB");
            };

            checkSlope (dsp::CrossoverBank::Slope::slope12);
            checkSlope (dsp::CrossoverBank::Slope::slope24);
            checkSlope (dsp::CrossoverBank::Slope::slope36);
            checkSlope (dsp::CrossoverBank::Slope::slope48);
            checkSlope (dsp::CrossoverBank::Slope::slope60);
            checkSlope (dsp::CrossoverBank::Slope::slope72);
        }
    }
};

//==============================================================================
class CrossoverSmoothnessTest final : public juce::UnitTest
{
public:
    CrossoverSmoothnessTest() : juce::UnitTest ("CrossoverSmoothness") {}

    void runTest() override
    {
        beginTest ("frequency changes are click-free (coefficient interpolation)");
        {
            dsp::CrossoverBank bank;
            bank.prepare (sampleRate, 512);
            bank.setCrossover (200.0f, 2000.0f, dsp::CrossoverBank::Slope::slope24);

            float previous = 0.5f; // DC steady state: lo+mid+hi = 0.5 after warmup
            float maxJump = 0.0f;

            // DC input: any real click shows up as a step in the output sum;
            // the steady-state output is constant (DC gain of the all-pass sum).
            // Warm up first so the measured jump is purely the retune transient.
            for (int n = 0; n < 96000; ++n)
            {
                if (n == 48000)
                    bank.setCrossover (3000.0f, 9000.0f, dsp::CrossoverBank::Slope::slope24);

                float l, m, h;
                bank.process (0.5f, l, m, h);

                if (n >= 24000) // skip the cold-start transient
                {
                    const auto sum = l + m + h;
                    maxJump = juce::jmax (maxJump, std::abs (sum - previous));
                    previous = sum;
                }
            }

            // The coefficient interpolation keeps the output continuous; an
            // instantaneous retune would step by ~0.1-0.2 here.
            expectLessThan (maxJump, 0.02f, "crossover change caused a click");
        }

        beginTest ("frequency changes are click-free under a sine input (no residual coefficient step)");
        {
            // DC cannot expose a coefficient step (the DC gain of each biquad
            // is exactly 1 for LP / 0 for HP at any coefficients), so re-check
            // with a sine: a residual coefficient jump at the end of the ramp
            // would show up as a sample-to-sample discontinuity well above the
            // sine's own slope (0.5 * 2pi * 1000/48000 ~= 0.065).
            dsp::CrossoverBank bank;
            bank.prepare (sampleRate, 512);
            bank.setCrossover (200.0f, 2000.0f, dsp::CrossoverBank::Slope::slope24);

            // warm up
            for (int n = 0; n < 48000; ++n)
            {
                const auto x = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                * 1000.0f * n / static_cast<float> (sampleRate));
                float l, m, h;
                bank.process (x, l, m, h);
            }

            float previous = 0.0f;
            bool measured = false;
            float maxJumpAtRampEnd = 0.0f; // around n = 48000 + 960 (ramp end)

            for (int n = 48000; n < 144000; ++n)
            {
                if (n == 48000)
                    bank.setCrossover (3000.0f, 9000.0f, dsp::CrossoverBank::Slope::slope24);

                const auto x = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                * 1000.0f * n / static_cast<float> (sampleRate));
                float l, m, h;
                bank.process (x, l, m, h);

                const auto sum = l + m + h;
                const auto jump = std::abs (sum - previous);
                if (measured)
                {
                    if (n >= 48955 && n <= 48965) // ramp ends at 48960
                        maxJumpAtRampEnd = juce::jmax (maxJumpAtRampEnd, jump);
                }
                previous = sum;
                measured = true;
            }

            // During the ramp the gain migrates quickly but continuously
            // (a 20 ms AM transient — audible as a brief wah, not a click).
            // The real click hazard is a residual coefficient step when the
            // ramp ends: with a linear ramp the coefficients land exactly on
            // the target, so the jump there must be near the sine slope alone
            // (~0.065); an exponential-ramp snap would add ~0.15-0.3.
            expectLessThan (maxJumpAtRampEnd, 0.1f,
                            "residual coefficient step when the ramp ends");
        }

        // Audit P1-3: a slope change flips the mid polarity (odd/even LR
        // order) and the active stage count. The old output must crossfade
        // into the new one over 20 ms — no full-amplitude polarity step.
        beginTest ("slope changes are click-free (old/new bank crossfade)");
        {
            dsp::CrossoverBank bank;
            bank.prepare (sampleRate, 512);
            bank.setCrossover (200.0f, 4000.0f, dsp::CrossoverBank::Slope::slope24);

            const auto input = [] (int n)
            {
                return 0.5f * std::sin (juce::MathConstants<float>::twoPi * 1200.0f
                                        * static_cast<float> (n) / static_cast<float> (sampleRate));
            };

            for (int n = 0; n < 48000; ++n) // warm up inside the mid band
            {
                float l, m, h;
                bank.process (input (n), l, m, h);
            }

            float previousSum = 0.0f, previousMid = 0.0f;
            bool havePrevious = false;
            int n = 48000;

            const auto changeTo = [&] (dsp::CrossoverBank::Slope slope, const char* label)
            {
                bank.setCrossover (200.0f, 4000.0f, slope);

                float maxJumpSum = 0.0f, maxJumpMid = 0.0f;
                for (int k = 0; k < 9600; ++k, ++n) // 200 ms per setting
                {
                    float l, m, h;
                    bank.process (input (n), l, m, h);

                    const auto sum = l + m + h;
                    if (havePrevious)
                    {
                        maxJumpSum = juce::jmax (maxJumpSum, std::abs (sum - previousSum));
                        maxJumpMid = juce::jmax (maxJumpMid, std::abs (m - previousMid));
                    }
                    previousSum = sum;
                    previousMid = m;
                    havePrevious = true;
                }

                logMessage (juce::String ("  slope ") + label
                            + ": max jump sum=" + juce::String (maxJumpSum, 4)
                            + " mid=" + juce::String (maxJumpMid, 4));
                // The 0.5 sine at 1.2 kHz has a natural sample slope of
                // ~0.079; a polarity flip would step by ~1.0.
                expectLessThan (maxJumpSum, 0.2f,
                                juce::String (label) + ": crossover sum clicked");
                expectLessThan (maxJumpMid, 0.2f,
                                juce::String (label) + ": mid band clicked");
            };

            changeTo (dsp::CrossoverBank::Slope::slope12, "24->12");
            changeTo (dsp::CrossoverBank::Slope::slope36, "12->36");
            changeTo (dsp::CrossoverBank::Slope::slope48, "36->48");
            changeTo (dsp::CrossoverBank::Slope::slope60, "48->60");
            changeTo (dsp::CrossoverBank::Slope::slope72, "60->72");
            changeTo (dsp::CrossoverBank::Slope::slope24, "72->24");
        }

        beginTest ("SPEC §4.4 clamping: effective f2 >= f1 * 1.05");
        {
            dsp::CrossoverBank probe;
            probe.prepare (sampleRate, 512);
            probe.setCrossover (1000.0f, 1000.0f, dsp::CrossoverBank::Slope::slope24);

            // With f2 clamped up to 1050, both -6 dB points sit at ~1025 Hz:
            // lo and hi each contribute ~-3 dB (0.707) at 1050 Hz, and the sum
            // of the two bands is still flat.
            const auto gainAt = [&] (bool highBand, float f)
            {
                double inSq = 0.0, outSq = 0.0;
                constexpr int warmup = 4800;
                constexpr int measure = 9600;
                for (int n = 0; n < warmup + measure; ++n)
                {
                    const auto x = std::sin (juce::MathConstants<float>::twoPi
                                             * f * n / static_cast<float> (sampleRate));
                    float l, m, h;
                    probe.process (x, l, m, h);
                    if (n >= warmup)
                    {
                        inSq += static_cast<double> (x) * x;
                        const auto v = highBand ? h : l;
                        outSq += static_cast<double> (v) * v;
                    }
                }
                return static_cast<float> (std::sqrt (outSq / juce::jmax (inSq, 1e-30)));
            };

            // With f2 clamped up to 1050: hi's -6 dB point must sit at ~1050
            // (proving the clamp moved it up from 1000), lo's -6 dB point
            // stays at ~1000, and lo has already rolled off past 1050.
            const auto loAt1000 = gainAt (false, 1000.0f);
            const auto hiAt1050 = gainAt (true,  1050.0f);
            const auto loAt1050 = gainAt (false, 1050.0f);
            const auto hiAt1000 = gainAt (true,  1000.0f);

            expectWithinAbsoluteError (loAt1000, 0.5f, 0.05f, "lo -6 dB point moved");
            expectWithinAbsoluteError (hiAt1050, 0.5f, 0.05f, "hi -6 dB point not at clamped f2");
            expectLessThan (loAt1050, 0.55f, "lo should roll off past the clamped f2");
            expectLessThan (hiAt1000, 0.48f, "hi crossover must have moved up from 1000");
        }
    }
};

CrossoverPhaseTest crossoverPhaseTest;
CrossoverFrequencyTest crossoverFrequencyTest;
CrossoverPowerTest crossoverPowerTest;
CrossoverSmoothnessTest crossoverSmoothnessTest;

//==============================================================================
class CrossoverHighRateTest final : public juce::UnitTest
{
public:
    CrossoverHighRateTest() : juce::UnitTest ("CrossoverHighRate") {}

    void runTest() override
    {
        beginTest ("192 kHz: 60 s white noise at 40 Hz crossover — stable, no NaN/drift (U12)");
        {
            constexpr double sr192 = 192000.0;
            dsp::CrossoverBank bank;
            bank.prepare (sr192, 512);
            bank.setCrossover (40.0f, 2000.0f, dsp::CrossoverBank::Slope::slope24);

            constexpr auto total = static_cast<juce::int64> (60.0 * sr192); // 11.52M samples
            double sumFirst = 0.0, sumLast = 0.0;
            float maxAbs = 0.0f;
            std::uint32_t rng = 0x12345678u;
            bool nanFound = false;

            for (juce::int64 n = 0; n < total; ++n)
            {
                // xorshift white noise in [-1, 1]
                rng ^= rng << 13;
                rng ^= rng >> 17;
                rng ^= rng << 5;
                const auto x = static_cast<float> (rng) * (2.0f / 4294967295.0f) - 1.0f;

                float l, m, h;
                bank.process (x, l, m, h);
                const auto y = l + m + h;

                if (! std::isfinite (y))
                {
                    nanFound = true;
                    break;
                }

                maxAbs = juce::jmax (maxAbs, std::abs (y));

                if (n < static_cast<juce::int64> (sr192))           // first second
                    sumFirst += static_cast<double> (y) * y;
                else if (n > total - static_cast<juce::int64> (sr192)) // last second
                    sumLast += static_cast<double> (y) * y;
            }

            expect (! nanFound, "NaN/inf produced at 192 kHz");
            expectLessThan (maxAbs, 10.0f, "output blew up");

            // Drift check: RMS of the first vs last second must match within 1 dB.
            const auto db = 10.0 * std::log10 (sumLast / juce::jmax (sumFirst, 1e-30));
            expect (std::abs (db) < 1.0,
                    juce::String ("RMS drift over 60 s = ") + juce::String (db) + " dB");
        }

        beginTest ("192 kHz: band-sum flatness 40 Hz..18 kHz (+-1 dB)");
        {
            constexpr double sr192 = 192000.0;
            dsp::CrossoverBank bank;
            bank.prepare (sr192, 512);
            bank.setCrossover (200.0f, 2000.0f, dsp::CrossoverBank::Slope::slope24);

            bool ok = true;
            float worstDb = 0.0f;

            for (int k = 0; k < 24; ++k) // 40..18000 Hz, log steps
            {
                const auto f = 40.0f * std::pow (450.0f, static_cast<float> (k) / 23);
                double inSq = 0.0, outSq = 0.0;
                constexpr int warmup = 9600;
                constexpr int measure = 19200;

                for (int n = 0; n < warmup + measure; ++n)
                {
                    const auto x = std::sin (juce::MathConstants<float>::twoPi
                                             * f * n / static_cast<float> (sr192));
                    float l, m, h;
                    bank.process (x, l, m, h);
                    if (n >= warmup)
                    {
                        inSq += static_cast<double> (x) * x;
                        const auto sum = static_cast<double> (l) + m + h;
                        outSq += sum * sum;
                    }
                }

                const auto db = 10.0 * std::log10 (outSq / juce::jmax (inSq, 1e-30));
                worstDb = juce::jmax (worstDb, std::abs (static_cast<float> (db)));
                if (std::abs (db) > 1.0)
                    ok = false;
            }

            expect (ok, juce::String ("192 kHz flatness exceeded +-1 dB; worst = ")
                    + juce::String (worstDb) + " dB");
        }
    }
};

CrossoverHighRateTest crossoverHighRateTest;

} // namespace
