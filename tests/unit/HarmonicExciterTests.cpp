/*
    NeoVoiDrive T09 tests — HarmonicExciter.

    Acceptance (TASKS.md T09):
      1. drive=0 -> bit-transparent (wet path skipped, R9);
      2. distortion rises monotonically as drive goes 1..10;
      3. tone filter peak frequency ~ set tone (+-20%);
      4. aliased images of the tanh harmonics above the output Nyquist
         stay below -40 dB (2x oversampling, DECISIONS §3.2);
      5. measured group delay < 10 samples (R11);
      6. zero allocation (structural).
*/

#include <juce_core/juce_core.h>

#include "dsp/HarmonicExciter.h"

#include <cmath>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;

//==============================================================================
//  Single-frequency DFT magnitude (integer periods -> no leakage).
//==============================================================================
double dftMagnitude (const std::vector<float>& signal, double freqHz, int start, int numSamples)
{
    double re = 0.0, im = 0.0;
    const auto w = juce::MathConstants<double>::twoPi * freqHz / kSampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto phase = w * (start + n);
        const auto v = static_cast<double> (signal[start + n]);
        re += v * std::cos (phase);
        im -= v * std::sin (phase);
    }

    return std::sqrt (re * re + im * im) * 2.0 / numSamples;
}

//==============================================================================
//  Complex DFT of one frequency (integer periods -> no leakage).
//==============================================================================
void dftComplex (const std::vector<float>& signal, double freqHz, int start, int numSamples,
                 double& re, double& im)
{
    re = 0.0;
    im = 0.0;
    const auto w = juce::MathConstants<double>::twoPi * freqHz / kSampleRate;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto phase = w * (start + n);
        const auto v = static_cast<double> (signal[start + n]);
        re += v * std::cos (phase);
        im -= v * std::sin (phase);
    }
}

//==============================================================================
class HarmonicExciterTest final : public juce::UnitTest
{
public:
    HarmonicExciterTest() : juce::UnitTest ("HarmonicExciter") {}

    void runTest() override
    {
        beginTest ("drive=0 equals input delayed by the oversampler (latency-aligned)");
        {
            dsp::HarmonicExciter exciter;
            exciter.prepare (kSampleRate, 512);

            dsp::HarmonicExciter::Params params; // drive 0 by default
            exciter.setParameters (params);

            // M2 (T13 review): drive=0 returns the latency-matched dry so
            // adjacent bands stay phase-aligned; content stays transparent
            // but the output is delayed by the oversampler latency.
            const auto latency = exciter.getLatencySamples();
            expectGreaterThan (latency, 0);

            constexpr int ringSize = 16;
            float history[ringSize] = {};
            bool aligned = true;
            for (int n = 0; n < 48000; ++n)
            {
                const auto x = std::sin (0.01f * static_cast<float> (n)) * 0.8f;
                const auto y = exciter.process (x);
                const auto expected = history[static_cast<size_t> ((n + ringSize - latency) % ringSize)];
                history[static_cast<size_t> (n % ringSize)] = x;
                if (y != expected)
                {
                    aligned = false;
                    break;
                }
            }

            expect (aligned, "drive=0 must equal the input delayed by the oversampler");
        }

        beginTest ("THD rises monotonically with drive (1..10 gain)");
        {
            const auto measureTHD = [] (float drivePercent)
            {
                dsp::HarmonicExciter exciter;
                exciter.prepare (kSampleRate, 512);

                dsp::HarmonicExciter::Params params;
                params.drive = drivePercent;
                params.tone  = 3000.0f; // wide-ish band around 1 kHz harmonics
                exciter.setParameters (params);

                constexpr float inputFreq = 1000.0f;
                constexpr int total = 3 * 48000;
                std::vector<float> out (total);

                for (int n = 0; n < total; ++n)
                {
                    const auto x = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                    * inputFreq * n / static_cast<float> (kSampleRate));
                    out[n] = exciter.process (x);
                }

                // measure on the last second (settled): 48000 samples = 1000 periods
                const auto fundamental = dftMagnitude (out, inputFreq, 2 * 48000, 48000);
                double harmonicsSq = 0.0;
                for (int k = 2; k <= 8; ++k)
                {
                    const auto h = dftMagnitude (out, k * inputFreq, 2 * 48000, 48000);
                    harmonicsSq += h * h;
                }

                return static_cast<float> (std::sqrt (harmonicsSq) / fundamental);
            };

            const auto thd10  = measureTHD (10.0f);
            const auto thd30  = measureTHD (30.0f);
            const auto thd50  = measureTHD (50.0f);
            const auto thd70  = measureTHD (70.0f);
            const auto thd100 = measureTHD (100.0f);

            expectGreaterThan (thd10, 0.001f, "drive 10% should produce some distortion");
            expectGreaterThan (thd30, thd10, "THD(30) should exceed THD(10)");
            expectGreaterThan (thd50, thd30, "THD(50) should exceed THD(30)");
            expectGreaterThan (thd70, thd50, "THD(70) should exceed THD(50)");
            expectGreaterThan (thd100, thd70, "THD(100) should exceed THD(70)");
        }

        beginTest ("tone filter peak frequency matches the set tone (+-20%)");
        {
            const auto findPeak = [] (float toneHz)
            {
                dsp::HarmonicExciter exciter;
                exciter.prepare (kSampleRate, 512);

                dsp::HarmonicExciter::Params params;
                params.drive = 10.0f;  // small: tanh stays near-linear
                params.tone  = toneHz;
                exciter.setParameters (params);

                constexpr float probeAmp = 0.01f;

                float bestFreq = 0.0f;
                float bestRms = -1.0f;

                // sweep 500..8000 Hz in 250 Hz steps
                for (float f = 500.0f; f <= 8000.0f; f += 250.0f)
                {
                // reset filter state per probe
                {
                    exciter.prepare (kSampleRate, 512);
                    exciter.setParameters (params);
                }

                    double sumSq = 0.0;
                    constexpr int warmup = 2400; // 50 ms: let smoothing/filter settle
                    constexpr int measure = 4800;
                    for (int n = 0; n < warmup + measure; ++n)
                    {
                        const auto x = probeAmp * std::sin (juce::MathConstants<float>::twoPi
                                                            * f * n / static_cast<float> (kSampleRate));
                        const auto y = exciter.process (x);
                        if (n >= warmup)
                            sumSq += static_cast<double> (y) * y;
                    }

                    const auto rms = static_cast<float> (std::sqrt (sumSq / measure));
                    if (rms > bestRms)
                    {
                        bestRms = rms;
                        bestFreq = f;
                    }
                }

                return bestFreq;
            };

            const auto peak2k = findPeak (2000.0f);
            const auto peak5k = findPeak (5000.0f);

            expectWithinAbsoluteError (peak2k, 2000.0f, 400.0f,  // +-20%
                                       "tone=2000 peak at " + juce::String (peak2k));
            expectWithinAbsoluteError (peak5k, 5000.0f, 1000.0f, // +-20%
                                       "tone=5000 peak at " + juce::String (peak5k));
        }

        beginTest ("aliased images of tanh harmonics stay below -40 dB (2x oversampling)");
        {
            // Worst case from the S1 review: 7 kHz input, drive=100%, tone=10 kHz.
            // Without oversampling the 5th harmonic (35 kHz) folded back to
            // 13 kHz at -14.5 dB; with 2x oversampling it must be < -40 dB
            // relative to the fundamental (T09 acceptance).
            dsp::HarmonicExciter exciter;
            exciter.prepare (kSampleRate, 512);

            dsp::HarmonicExciter::Params params;
            params.drive = 100.0f;
            params.tone  = 10000.0f;
            exciter.setParameters (params);

            constexpr float inputFreq = 7000.0f;
            constexpr int total = 48000;
            std::vector<float> out (total);

            for (int n = 0; n < total; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi
                                         * inputFreq * n / static_cast<float> (kSampleRate));
                out[static_cast<size_t> (n)] = exciter.process (x);
            }

            const auto fund = dftMagnitude (out, inputFreq, 24000, 24000);
            const auto folded = dftMagnitude (out, 13000.0, 24000, 24000); // 35 kHz folded

            expectGreaterThan (static_cast<float> (fund), 0.01f, "fundamental vanished");
            expectLessThan (static_cast<float> (folded / fund), 0.01f, // -40 dB
                            "aliased image at 13 kHz exceeds -40 dB");
        }

        beginTest ("measured group delay < 10 samples (R11)");
        {
            dsp::HarmonicExciter exciter;
            exciter.prepare (kSampleRate, 512);

            dsp::HarmonicExciter::Params params;
            params.drive = 100.0f; // fully wet: the delay is the whole chain
            params.tone  = 10000.0f;
            exciter.setParameters (params);

            // Group delay via the phase difference of two probe frequencies
            // (both integer periods over 48000 samples -> no leakage).
            // A single-frequency phase delay would be polluted by the tone
            // filter's phase response (bandpass leads at f << tone).
            const auto phaseAt = [&] (float probeFreq)
            {
                dsp::HarmonicExciter probe;
                probe.prepare (kSampleRate, 512);
                probe.setParameters (params);

                constexpr int total = 48000;
                std::vector<float> out (total);
                for (int n = 0; n < total; ++n)
                {
                    const auto x = std::sin (juce::MathConstants<float>::twoPi
                                             * probeFreq * n / static_cast<float> (kSampleRate));
                    out[static_cast<size_t> (n)] = probe.process (x);
                }

                double re, im;
                dftComplex (out, probeFreq, 24000, 24000, re, im);
                return std::atan2 (im, re);
            };

            constexpr float f1 = 1000.0f;
            constexpr float f2 = 1500.0f;
            const auto phi1 = phaseAt (f1);
            const auto phi2 = phaseAt (f2);
            const auto dw = juce::MathConstants<double>::twoPi * (f2 - f1) / kSampleRate;

            // tau = -d(phi)/d(w).  With tau < 10 the phase difference is far
            // below 2*pi, so no unwrapping is needed.
            const auto delay = -(phi2 - phi1) / dw;

            expectGreaterThan (static_cast<float> (delay), -0.5f,
                               "negative group delay (dry path misaligned?)");
            expectLessThan (static_cast<float> (delay), 10.0f,
                            "group delay " + juce::String (delay) + " samples exceeds 10");
        }

        beginTest ("drive and tone smoothing ramps are active");
        {
            dsp::HarmonicExciter exciter;
            exciter.prepare (kSampleRate, 512);

            dsp::HarmonicExciter::Params params;
            params.drive = 50.0f;
            params.tone  = 6000.0f;
            exciter.setParameters (params);

            for (int i = 0; i < 480; ++i) // 10 ms @ 48 kHz (drive)
                exciter.process (0.0f);

            expectWithinAbsoluteError (exciter.getSmoothedDrive(), 50.0f, 0.2f,
                                       "drive did not settle within 10 ms");

            for (int i = 0; i < 960; ++i) // 20 ms @ 48 kHz (tone)
                exciter.process (0.0f);

            expectWithinAbsoluteError (exciter.getSmoothedTone(), 6000.0f, 1.0f,
                                       "tone did not settle within 20 ms");
        }

        beginTest ("P3: tone biquad coefficients refresh at control rate");
        {
            dsp::HarmonicExciter exciter;
            exciter.prepare (kSampleRate, 512);

            dsp::HarmonicExciter::Params params;
            params.drive = 50.0f;
            params.tone  = 3000.0f;
            exciter.setParameters (params);

            for (int i = 0; i < 48000; ++i) // 1 s
                exciter.process (0.1f * std::sin (juce::MathConstants<float>::twoPi
                                                  * 440.0f * static_cast<float> (i)
                                                  / static_cast<float> (kSampleRate)));

            const auto updates = exciter.getToneCoefficientUpdates();
            logMessage (juce::String ("  tone coefficient updates / 1 s: ")
                        + juce::String (updates));
            expectGreaterThan (updates, 0, "coefficients were never refreshed");
            expectLessOrEqual (updates, 48000 / 32 + 2,
                               "coefficients are still refreshed per sample");
            expectLessThan (updates, 48000 / 8,
                            "coefficient refresh rate is higher than the control rate");
        }
    }
};

HarmonicExciterTest harmonicExciterTest;

} // namespace
