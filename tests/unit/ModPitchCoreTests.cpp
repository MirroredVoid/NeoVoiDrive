/*
    NeoVoiDrive T06 tests — ModLFO + ModDelayLine pitch modulation core.

    Acceptance (TASKS.md T06):
      1. sine LFO + sine input: measured pitch swing == set cents (+-5%),
         offline frequency-measurement method (DECISIONS §1.3 approximation);
      2. square output has bounded derivative (slew, no click, R5);
      3. FullWave effective frequency == 2x rate;
      4. capacity satisfies SPEC §5.2 (8192 @ 192 kHz);
      5. zero allocation (structural: fixed std::array).
*/

#include <juce_core/juce_core.h>

#include "dsp/ModDelayLine.h"
#include "dsp/ModLFO.h"

#include <cmath>
#include <vector>

namespace
{

constexpr double sampleRate = 48000.0;

//==============================================================================
//  Zero-crossing based frequency measurement over a window centred on a sample.
//  Uses linearly interpolated crossing times for sub-sample accuracy.
//==============================================================================
float measureFrequency (const std::vector<float>& signal, int centreSample, int halfWindow)
{
    const auto start = centreSample - halfWindow;
    const auto end   = centreSample + halfWindow;

    std::vector<double> crossings;

    for (int n = start + 1; n < end; ++n)
    {
        const auto a = signal[n - 1];
        const auto b = signal[n];

        if ((a < 0.0f) != (b < 0.0f))
        {
            const auto frac = static_cast<double> (a) / (static_cast<double> (a) - b);
            crossings.push_back (static_cast<double> (n - 1) + frac);
        }
    }

    if (crossings.size() < 2)
        return 0.0f;

    // Average frequency over the measured span.
    const auto span = crossings.back() - crossings.front();
    return static_cast<float> ((crossings.size() - 1) * sampleRate / (2.0 * span));
}

//==============================================================================
class ModLFOTest final : public juce::UnitTest
{
public:
    ModLFOTest() : juce::UnitTest ("ModLFO") {}

    void runTest() override
    {
        beginTest ("waveform ranges: sine/square/noise in [-1,1], fullWave in [0,1]");
        {
            const auto checkRange = [] (dsp::ModLFO::Waveform w, float lo, float hi)
            {
                dsp::ModLFO lfo;
                lfo.prepare (sampleRate);
                lfo.setWaveform (w);
                lfo.setRate (10.0f);

                bool inRange = true;
                for (int i = 0; i < 48000; ++i)
                {
                    const auto v = lfo.next();
                    if (! (v >= lo && v <= hi))
                    {
                        inRange = false;
                        break;
                    }
                }
                return inRange;
            };

            expect (checkRange (dsp::ModLFO::Waveform::sine,    -1.0f, 1.0f));
            expect (checkRange (dsp::ModLFO::Waveform::square,  -1.0f, 1.0f));
            expect (checkRange (dsp::ModLFO::Waveform::fullWave, 0.0f, 1.0f));
            expect (checkRange (dsp::ModLFO::Waveform::noise,   -1.0f, 1.0f));
        }

        beginTest ("sine output has exactly `rate` peaks per second");
        {
            dsp::ModLFO lfo;
            lfo.prepare (sampleRate);
            lfo.setWaveform (dsp::ModLFO::Waveform::sine);
            lfo.setRate (10.0f);

            float previous = lfo.next();
            int peaks = 0;
            for (int i = 1; i < 48000; ++i)
            {
                const auto current = lfo.next();
                if (previous > 0.0f && current <= 0.0f) // falling zero crossing
                    ++peaks;
                previous = current;
            }

            expectEquals (peaks, 10);
        }

        beginTest ("fullWave effective frequency is 2x rate (peak count)");
        {
            dsp::ModLFO lfo;
            lfo.prepare (sampleRate);
            lfo.setWaveform (dsp::ModLFO::Waveform::fullWave);
            lfo.setRate (10.0f);

            std::vector<float> buf;
            buf.reserve (48000);
            for (int i = 0; i < 48000; ++i)
                buf.push_back (lfo.next());

            int peaks = 0;
            for (int i = 1; i < 48000 - 1; ++i)
                if (buf[i] > buf[i - 1] && buf[i] >= buf[i + 1])
                    ++peaks;

            expectEquals (peaks, 20); // 2x the 10 Hz rate
        }

        beginTest ("square edges are slew-limited (bounded derivative, R5)");
        {
            dsp::ModLFO lfo;
            lfo.prepare (sampleRate);
            lfo.setWaveform (dsp::ModLFO::Waveform::square);
            lfo.setRate (10.0f);
            lfo.setSlewMs (1.0f);

            // theoretical max step: full swing 2.0 over 1 ms
            const auto maxStep = static_cast<float> (2.0 / (0.001 * sampleRate));

            float previous = lfo.next();
            float maxAbsDelta = 0.0f;
            for (int i = 1; i < 48000; ++i)
            {
                const auto current = lfo.next();
                maxAbsDelta = juce::jmax (maxAbsDelta, std::abs (current - previous));
                previous = current;
            }

            expectLessThan (maxAbsDelta, maxStep * 1.001f + 1e-6f);
            expectGreaterThan (maxAbsDelta, maxStep * 0.5f); // edges did happen

            // ramp length: 2.0 / maxStep == 1 ms worth of samples
            const auto rampSamples = static_cast<int> (2.0f / maxStep);
            expectEquals (rampSamples, 48); // 1 ms @ 48 kHz
        }
    }
};

//==============================================================================
class ModDelayLineTest final : public juce::UnitTest
{
public:
    ModDelayLineTest() : juce::UnitTest ("ModDelayLine") {}

    void runTest() override
    {
        beginTest ("capacity is 8192 samples (SPEC §5.2)");
        {
            expectEquals (dsp::ModDelayLine::maxDelaySamples, 8192);
            expectEquals (dsp::ModDelayLine::getMaxDelaySamples(), 8191);
        }

        beginTest ("zero delay is bit-transparent");
        {
            dsp::ModDelayLine line;
            line.prepare (sampleRate, 512);

            for (int n = 0; n < 1000; ++n)
            {
                const auto x = std::sin (0.01f * n);
                expectEquals (line.process (x, 0.0f), x);
            }
        }

        beginTest ("integer delay shifts the signal exactly");
        {
            dsp::ModDelayLine line;
            line.prepare (sampleRate, 512);

            constexpr int delay = 4;
            for (int n = 0; n < 1000; ++n)
            {
                const auto x = static_cast<float> (n);
                const auto y = line.process (x, static_cast<float> (delay));
                if (n >= delay)
                    expectEquals (y, static_cast<float> (n - delay));
            }
        }

        beginTest ("fractional delay matches the ideal interpolated signal");
        {
            dsp::ModDelayLine line;
            line.prepare (sampleRate, 512);

            constexpr float inputFreq = 1000.0f;
            constexpr float delay = 4.5f;
            constexpr float tolerance = 2e-3f;

            for (int n = 0; n < 2000; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi * inputFreq * n / static_cast<float> (sampleRate));
                const auto y = line.process (x, delay);

                if (n > 100) // skip the zero-filled warm-up region
                {
                    const auto expected = std::sin (juce::MathConstants<float>::twoPi * inputFreq
                                                    * (n - delay) / static_cast<float> (sampleRate));
                    expectWithinAbsoluteError (y, expected, tolerance, "fractional delay mismatch at n=" + juce::String (n));
                }
            }
        }

        beginTest ("P2-6 depths above capacity clamp instead of wrapping");
        {
            // 384 kHz worst case (~13.7 k samples) exceeds the 8192-sample
            // line; the clamp must make it behave exactly like the maximum
            // usable depth (no read-pointer wrap into the wrong history).
            dsp::ModDelayLine clamped, atMax;
            clamped.prepare (sampleRate, 512);
            atMax.prepare (sampleRate, 512);

            constexpr float overCapacity = 13700.0f;
            const auto maxDepth = static_cast<float> (dsp::ModDelayLine::getMaxDelaySamples());

            for (int n = 0; n < 20000; ++n) // wraps ~2.4x
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                         * static_cast<float> (n) / static_cast<float> (sampleRate));
                const auto y = clamped.process (x, overCapacity);
                expectEquals (y, atMax.process (x, maxDepth));
                expect (std::isfinite (y), "clamped depth produced a non-finite sample");
            }
        }

        beginTest ("large delay near capacity wraps correctly");
        {
            dsp::ModDelayLine line;
            line.prepare (sampleRate, 512);

            constexpr int delay = 8190; // max usable
            for (int n = 0; n < 20000; ++n) // wraps ~2.4x
            {
                const auto x = static_cast<float> (n);
                const auto y = line.process (x, static_cast<float> (delay));
                if (n >= delay)
                    expectEquals (y, static_cast<float> (n - delay), "wrap-around corruption at n=" + juce::String (n));
            }
        }

        beginTest ("centsToSeconds implements DECISIONS §1.3 formula");
        {
            const auto hundred = dsp::ModDelayLine::centsToSeconds (100.0f, 10.0f);
            // (2^(100/1200) - 1) / (2*pi*10)
            const auto expected100 = (std::pow (2.0f, 100.0f / 1200.0f) - 1.0f) / (juce::MathConstants<float>::twoPi * 10.0f);
            expectWithinAbsoluteError (hundred, expected100, expected100 * 1e-4f);

            expectEquals (dsp::ModDelayLine::centsToSeconds (0.0f, 10.0f), 0.0f);

            // 1200 cents = octave: A = 1 / (2*pi*f)
            const auto octave = dsp::ModDelayLine::centsToSeconds (1200.0f, 10.0f);
            expectWithinAbsoluteError (octave, 1.0f / (juce::MathConstants<float>::twoPi * 10.0f), 1e-6f);
        }
    }
};

//==============================================================================
//  End-to-end: sine LFO driving the delay line, pitch swing measured by
//  zero-crossing frequency analysis (task acceptance #1).
//==============================================================================
class PitchSwingTest final : public juce::UnitTest
{
public:
    PitchSwingTest() : juce::UnitTest ("PitchSwing") {}

    void runTest() override
    {
        beginTest ("measured pitch swing matches set cents (+-5%)");
        {
            constexpr float inputFreq = 1000.0f;
            constexpr float lfoRate = 10.0f;
            constexpr float depthCents = 100.0f;
            constexpr float baseDelay = 64.0f;

            dsp::ModLFO lfo;
            lfo.prepare (sampleRate);
            lfo.setWaveform (dsp::ModLFO::Waveform::sine);
            lfo.setRate (lfoRate);

            dsp::ModDelayLine line;
            line.prepare (sampleRate, 512);

            const auto swingSeconds = dsp::ModDelayLine::centsToSeconds (depthCents, lfoRate);
            const auto swingSamples = static_cast<float> (swingSeconds * sampleRate);

            constexpr int totalSamples = 3 * 48000;
            std::vector<float> output (totalSamples);

            for (int n = 0; n < totalSamples; ++n)
            {
                const auto x = std::sin (juce::MathConstants<float>::twoPi * inputFreq * n / static_cast<float> (sampleRate));
                const auto m = lfo.next();
                output[n] = line.process (x, baseDelay + swingSamples * m);
            }

            // DECISIONS §1.3: r = 1 - dD/dt, peak |dD/dt| = 2^(c/1200) - 1
            const auto ratio = std::pow (2.0f, depthCents / 1200.0f) - 1.0f;
            const auto freqLow  = inputFreq * (1.0f - ratio); // theta = 0   (m rising through 0)
            const auto freqHigh = inputFreq * (1.0f + ratio); // theta = pi  (m falling through 0)

            constexpr int halfWindow = 240; // +-5 ms
            // Tight tolerance: the measurement window averages the instantaneous
            // frequency near the swing peak; the systematic bias is ~1 Hz
            // (window half-angle 0.314 rad -> mean(cos) ~= 0.984).
            const auto tolerance = 3.0f;

            // m = sin(2*pi*10*t) rises through 0 at t = k/10 -> n = k*4800
            for (int k = 2; k < 28; ++k)
            {
                const auto f = measureFrequency (output, k * 4800, halfWindow);
                expectWithinAbsoluteError (f, freqLow, tolerance,
                                           "low-side swing at k=" + juce::String (k) + ": f=" + juce::String (f));
            }

            // falls through 0 at t = (k + 0.5)/10 -> n = (2k+1)*2400
            for (int k = 2; k < 28; ++k)
            {
                const auto f = measureFrequency (output, (2 * k + 1) * 2400, halfWindow);
                expectWithinAbsoluteError (f, freqHigh, tolerance,
                                           "high-side swing at k=" + juce::String (k) + ": f=" + juce::String (f));
            }
        }
    }
};

ModLFOTest modLFOTest;
ModDelayLineTest modDelayLineTest;
PitchSwingTest pitchSwingTest;

} // namespace
