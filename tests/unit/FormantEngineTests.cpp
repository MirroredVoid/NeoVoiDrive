/*
    NeoVoiDrive T12/T68 tests — FormantEngine.

    Acceptance (TASKS.md T12/T68):
      1. arbitrary block sizes (1/7/127/512/8192) produce identical, continuous
         output (no periodic glitches);
      2. shift=0 vs the delay-aligned input: SNR >= 100 dB (T68: magnitude-only
         gain = 1 + OLA reconstructs the input; the old STFT-core gate was
         40 dB);
      3. steady-state process performs zero heap allocations (audit counter);
      4. +-12 st moves the spectral envelope in the right direction by the
         right amount (synthetic resonant-noise signal);
      5. getLatencySamples() == 2048, the first window of output is silence and
         the delayed input is reconstructed;
      6. T68 regression: -12 st keeps the 8 kHz air band within 6 dB of the
         input and does not change a pure tone's F0.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/FormantEngine.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

//==============================================================================
//  Global allocation audit — MUST live at global scope (C2323 otherwise).
//  Counts every operator new in the process; the tests compare the delta
//  around the measured section only.
//==============================================================================
std::int64_t g_allocCount = 0;

void* operator new (std::size_t size)
{
    ++g_allocCount;
    if (void* p = std::malloc (size))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    ++g_allocCount;
    if (void* p = std::malloc (size))
        return p;
    throw std::bad_alloc();
}

// Global audit.  Non-MSVC builds miss aligned allocations (under-count only,
// never false positives); nothrow new is not overridden (unused here).
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#if defined(_MSC_VER)
#include <malloc.h>

void* operator new (std::size_t size, std::align_val_t align)
{
    ++g_allocCount;
    if (void* p = _aligned_malloc (size, static_cast<std::size_t> (align)))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size, std::align_val_t align)
{
    ++g_allocCount;
    if (void* p = _aligned_malloc (size, static_cast<std::size_t> (align)))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p, std::align_val_t) noexcept { _aligned_free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { _aligned_free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { _aligned_free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { _aligned_free (p); }
#endif

namespace
{

constexpr double kSampleRate = 48000.0;

//==============================================================================
class FormantBlockSizeTest final : public juce::UnitTest
{
public:
    FormantBlockSizeTest() : juce::UnitTest ("FormantBlockSize") {}

    void runTest() override
    {
        beginTest ("arbitrary block sizes give bit-identical continuous output");
        {
            constexpr int total = 4 * 48000;
            std::vector<float> input (total);
            for (int n = 0; n < total; ++n)
                input[static_cast<size_t> (n)] = 0.5f * std::sin (
                    juce::MathConstants<float>::twoPi * 440.0f * n / static_cast<float> (kSampleRate));

            const auto runWithBlock = [&] (int block)
            {
                dsp::FormantEngine engine;
                engine.prepare (kSampleRate, 8192);
                engine.setShiftSemitones (3.0f);   // engage a non-trivial path
                engine.setQuefrencyMs (1.0f);

                std::vector<float> out (total, 0.0f);
                for (int pos = 0; pos < total; pos += block)
                {
                    const auto n = std::min (block, total - pos);
                    engine.process (input.data() + pos, out.data() + pos, n);
                }
                return out;
            };

            const auto reference = runWithBlock (512);

            for (const auto block : { 1, 7, 127, 8192 })
            {
                const auto out = runWithBlock (block);
                bool identical = true;
                for (int n = 0; n < total; ++n)
                {
                    if (out[static_cast<size_t> (n)] != reference[static_cast<size_t> (n)])
                    {
                        identical = false;
                        break;
                    }
                }
                expect (identical, juce::String ("block size ") + juce::String (block)
                        + " produced different output");
            }
        }
    }
};

//==============================================================================
class FormantSnrTest final : public juce::UnitTest
{
public:
    FormantSnrTest() : juce::UnitTest ("FormantSnr") {}

    void runTest() override
    {
        const auto measuredSnr = [&] (float quefrencyMs)
        {
            dsp::FormantEngine engine;
            engine.prepare (kSampleRate, 8192);
            engine.setShiftSemitones (0.0f);
            engine.setQuefrencyMs (quefrencyMs);

            constexpr int total = 4 * 48000;
            std::vector<float> input (total), out (total, 0.0f);
            for (int n = 0; n < total; ++n)
                input[static_cast<size_t> (n)] = 0.5f * std::sin (
                    juce::MathConstants<float>::twoPi * 440.0f * n / static_cast<float> (kSampleRate));

            engine.process (input.data(), out.data(), total);

            // Compare from sample 4096 onwards (skip the latency plus
            // settling), output delayed by exactly `latencySamples`.
            constexpr int start = 4096;
            constexpr int latency = dsp::FormantEngine::latencySamples;
            double signalSq = 0.0, noiseSq = 0.0;
            for (int n = start; n + latency < total; ++n)
            {
                const auto ref = input[static_cast<size_t> (n)];
                const auto y = out[static_cast<size_t> (n + latency)];
                signalSq += static_cast<double> (ref) * ref;
                const auto err = static_cast<double> (y) - ref;
                noiseSq += err * err;
            }
            return 10.0 * std::log10 (signalSq / juce::jmax (noiseSq, 1e-30));
        };

        beginTest ("shift=0: SNR vs delay-aligned input >= 100 dB (T68)");
        {
            const auto snrEnvelope = measuredSnr (0.5f);
            expectGreaterThan (static_cast<float> (snrEnvelope), 100.0f,
                               "envelope-on SNR " + juce::String (snrEnvelope)
                               + " dB below the 100 dB gate");

            const auto snrBypass = measuredSnr (0.0f);
            expectGreaterThan (static_cast<float> (snrBypass), 100.0f,
                               "envelope-off SNR " + juce::String (snrBypass)
                               + " dB below the 100 dB gate");
        }
    }
};

//==============================================================================
class FormantAllocationTest final : public juce::UnitTest
{
public:
    FormantAllocationTest() : juce::UnitTest ("FormantAllocation") {}

    void runTest() override
    {
        beginTest ("steady-state process performs zero heap allocations");
        {
            dsp::FormantEngine engine;
            engine.prepare (kSampleRate, 8192);
            engine.setShiftSemitones (4.0f);
            engine.setQuefrencyMs (2.0f);

            // warm the engine thoroughly (frames + caches)
            constexpr int warmup = 3 * 48000;
            std::vector<float> in (warmup, 0.1f), out (warmup, 0.0f);
            engine.process (in.data(), out.data(), warmup);

            constexpr int measure = 2 * 48000;
            std::vector<float> in2 (measure, 0.0f), out2 (measure, 0.0f);
            for (int n = 0; n < measure; ++n)
                in2[static_cast<size_t> (n)] = 0.3f * std::sin (
                    juce::MathConstants<float>::twoPi * 220.0f * n / static_cast<float> (kSampleRate));

            const auto before = g_allocCount;
            engine.process (in2.data(), out2.data(), measure);
            const auto allocated = g_allocCount - before;

            expectEquals (static_cast<int> (allocated), 0,
                          juce::String ("process allocated ") + juce::String (allocated)
                          + " times");
        }
    }
};

//==============================================================================
class FormantEnvelopeTest final : public juce::UnitTest
{
public:
    FormantEnvelopeTest() : juce::UnitTest ("FormantEnvelope") {}

    void runTest() override
    {
        beginTest ("+-12 st moves the spectral envelope in the right direction");
        {
            // Input: a 1/f-ish noise source (white noise through a one-pole
            // low-pass).  A formant shift must move the spectral centroid up
            // for +12 st and down for -12 st.  A centroid is used instead of
            // three DFT bins because a narrowband carrier cannot follow an
            // envelope warp: only its residual energy is reweighted.
            constexpr int total = 8 * 48000;
            std::vector<float> input (total), out (total, 0.0f);

            std::uint32_t rng = 0x12345678u;
            double state = 0.0;
            for (int n = 0; n < total; ++n)
            {
                rng ^= rng << 13;
                rng ^= rng >> 17;
                rng ^= rng << 5;
                const auto white = static_cast<double> (rng) * (2.0 / 4294967295.0) - 1.0;
                state = 0.99 * state + 0.01 * white;
                input[static_cast<size_t> (n)] = static_cast<float> (state);
            }

            const auto runShift = [&] (float semitones, std::vector<float>& outBuf)
            {
                dsp::FormantEngine engine;
                engine.prepare (kSampleRate, 8192);
                engine.setShiftSemitones (semitones);
                engine.setQuefrencyMs (4.0f);
                engine.process (input.data(), outBuf.data(), total);
            };

            const auto centroid = [&] (const std::vector<float>& buf)
            {
                constexpr int start = 6 * 48000;
                constexpr int n = 96000;
                constexpr int fftOrder = 17;          // 131072 >= n
                constexpr int fftN = 1 << fftOrder;

                juce::dsp::FFT fft (fftOrder);
                std::vector<float> work (static_cast<size_t> (fftN * 2), 0.0f);
                for (int i = 0; i < n; ++i)
                {
                    const auto w = 0.5f - 0.5f * std::cos (
                        juce::MathConstants<float>::twoPi * static_cast<float> (i)
                        / static_cast<float> (n - 1));
                    work[static_cast<size_t> (i)] =
                        buf[static_cast<size_t> (start + i)] * w;
                }

                fft.performRealOnlyForwardTransform (work.data(), false);

                double num = 0.0, den = 0.0;
                for (int k = 1; k < fftN / 2; ++k)
                {
                    const auto hz = static_cast<double> (k) * kSampleRate / fftN;
                    if (hz < 150.0 || hz > 5500.0)
                        continue;

                    const auto re = static_cast<double> (work[static_cast<size_t> (2 * k)]);
                    const auto im = static_cast<double> (work[static_cast<size_t> (2 * k + 1)]);
                    const auto p = re * re + im * im;
                    num += hz * p;
                    den += p;
                }
                return num / juce::jmax (den, 1e-30);
            };

            std::vector<float> outUp (total, 0.0f), outDown (total, 0.0f);
            runShift (+12.0f, outUp);
            runShift (-12.0f, outDown);

            const auto cIn = centroid (input);
            const auto cUp = centroid (outUp);
            const auto cDown = centroid (outDown);

            expectGreaterThan (static_cast<float> (cUp / cIn), 1.3f,
                               "centroid did not move up: " + juce::String (cUp)
                               + " / " + juce::String (cIn));
            expectLessThan (static_cast<float> (cDown / cIn), 0.8f,
                            "centroid did not move down: " + juce::String (cDown)
                            + " / " + juce::String (cIn));
        }
    }
};

//==============================================================================
class FormantQualityTest final : public juce::UnitTest
{
public:
    FormantQualityTest() : juce::UnitTest ("FormantQuality") {}

    void runTest() override
    {
        const auto amplitudeAt = [] (const std::vector<float>& buf, float freq, int start, int n)
        {
            double re = 0.0, im = 0.0;
            const auto w = juce::MathConstants<double>::twoPi * freq / kSampleRate;
            for (int i = 0; i < n; ++i)
            {
                const auto ph = w * (start + i);
                const auto v = static_cast<double> (buf[static_cast<size_t> (start + i)]);
                re += v * std::cos (ph);
                im -= v * std::sin (ph);
            }
            return 2.0 * std::sqrt (re * re + im * im) / n;
        };

        beginTest ("T68: a 8 kHz tone is bypassed by the formant band limit");
        {
            constexpr int total = 3 * 48000;
            std::vector<float> input (total), out (total, 0.0f);
            for (int n = 0; n < total; ++n)
                input[static_cast<size_t> (n)] = 0.5f * std::sin (
                    juce::MathConstants<float>::twoPi * 8000.0f * n / static_cast<float> (kSampleRate));

            dsp::FormantEngine engine;
            engine.prepare (kSampleRate, 8192);
            engine.setShiftSemitones (-12.0f);
            engine.setQuefrencyMs (0.5f);
            engine.process (input.data(), out.data(), total);

            constexpr int start = 48000;
            constexpr int n = 48000;
            const auto inAmp = amplitudeAt (input, 8000.0f, start, n);
            const auto outAmp = amplitudeAt (out, 8000.0f, start + dsp::FormantEngine::latencySamples, n);
            const auto ratioDb = 20.0 * std::log10 (outAmp / juce::jmax (inAmp, 1e-12));
            expectGreaterThan (ratioDb, -1.0, "8 kHz air band lost " + juce::String (ratioDb) + " dB");
        }

        beginTest ("T68: -12 st does not change a pure tone's F0");
        {
            constexpr int total = 3 * 48000;
            std::vector<float> input (total), out (total, 0.0f);
            for (int n = 0; n < total; ++n)
                input[static_cast<size_t> (n)] = 0.5f * std::sin (
                    juce::MathConstants<float>::twoPi * 440.0f * n / static_cast<float> (kSampleRate));

            dsp::FormantEngine engine;
            engine.prepare (kSampleRate, 8192);
            engine.setShiftSemitones (-12.0f);
            engine.setQuefrencyMs (0.5f);
            engine.process (input.data(), out.data(), total);

            constexpr int start = 48000;
            constexpr int n = 48000;
            const auto f0 = amplitudeAt (out, 440.0f, start + dsp::FormantEngine::latencySamples, n);
            const auto half = amplitudeAt (out, 220.0f, start + dsp::FormantEngine::latencySamples, n);
            const auto twice = amplitudeAt (out, 880.0f, start + dsp::FormantEngine::latencySamples, n);
            expectGreaterThan (f0, 20.0f * juce::jmax (half, twice),
                               "F0 moved: |440|=" + juce::String (f0)
                               + " |220|=" + juce::String (half)
                               + " |880|=" + juce::String (twice));
        }
    }
};

//==============================================================================
class FormantLatencyTest final : public juce::UnitTest
{
public:
    FormantLatencyTest() : juce::UnitTest ("FormantLatency") {}

    void runTest() override
    {
        beginTest ("latency is 2048 samples and the first window is silence");
        {
            dsp::FormantEngine engine;
            engine.prepare (kSampleRate, 8192);

            expectEquals (engine.getLatencySamples(), 2048);

            constexpr int total = 48000;
            std::vector<float> input (total), out (total, 0.0f);
            for (int n = 0; n < total; ++n)
                input[static_cast<size_t> (n)] = 0.5f;

            engine.process (input.data(), out.data(), total);

            // The output reads one full STFT window behind the input, so the
            // first 2048 samples are exactly silent and sample 2048 is the
            // delayed input start.
            constexpr int latency = dsp::FormantEngine::latencySamples;
            for (int n = 0; n < latency; ++n)
            {
                if (out[static_cast<size_t> (n)] != 0.0f)
                {
                    expect (false, "sample " + juce::String (n) + " not silent");
                    break;
                }
            }
            expectGreaterThan (std::abs (out[static_cast<size_t> (latency + 512)]), 0.1f,
                               "output did not start after the reported latency");
        }

        beginTest ("clear() flushes streaming state like a fresh engine");
        {
            dsp::FormantEngine engine;
            engine.prepare (kSampleRate, 8192);
            engine.setShiftSemitones (4.0f);

            constexpr int total = 48000;
            std::vector<float> input (total), out (total, 0.0f);
            for (int n = 0; n < total; ++n)
                input[static_cast<size_t> (n)] = 0.5f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * n / kSampleRate));

            // Run (fills rings/window), then clear.
            engine.process (input.data(), out.data(), 24000);
            expect (out[static_cast<size_t> (24000 - 1)] != 0.0f, "pre-clear output should not be silent");
            engine.clear();

            // After clear the engine behaves like a fresh one: leading silence.
            std::fill (out.begin(), out.end(), 0.0f);
            engine.process (input.data(), out.data(), total);
            constexpr int silentUntil = dsp::FormantEngine::latencySamples;
            for (int n = 0; n < silentUntil; ++n)
            {
                if (out[static_cast<size_t> (n)] != 0.0f)
                {
                    expect (false, "after clear, sample " + juce::String (n) + " not silent");
                    break;
                }
            }
        }
    }
};

FormantBlockSizeTest formantBlockSizeTest;
FormantSnrTest formantSnrTest;
FormantAllocationTest formantAllocationTest;
FormantEnvelopeTest formantEnvelopeTest;
FormantQualityTest formantQualityTest;
FormantLatencyTest formantLatencyTest;

} // namespace
