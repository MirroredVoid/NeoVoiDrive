/*
    FormantEngine implementation (T12, T68).

    Dedicated STFT spectral-envelope warp.  Self-contained: a formant-only
    effect does not need pitch shifting, phase-vocoder propagation or an
    envelope resampler whose downward path would zero the HF tail.

    Streaming model (causal, one sample in / one sample out):

        - input samples enter a ring; every hop (512) a 2048-sample window
          ending at the newest input sample is analysed;
        - the magnitude envelope is extracted with a cepstral lifter
          (quefrency parameter, 0.5 ms default), contrast-enhanced by 1.5x,
          warped by f -> f / ratio in the log-magnitude domain and multiplied
          back onto the ORIGINAL phase;
        - analysis and synthesis use the same periodic Hann window; the OLA
          sum at 4x overlap is exactly 1.5, so no normalisation buffer is
          needed;
        - output is read 2048 samples behind the input, i.e. one full STFT
          window of latency (unchanged from T12).

    The warp is band-limited (150..5500 Hz, 1 kHz cosine fades) so the
    fundamental/sub-bass and the air above 5.5 kHz are not dragged with the
    formants.  A smoothed per-frame energy match keeps the output loudness
    stable across shift values.

    At shift = 0 the gain is exactly 1; OLA reconstructs the delayed input to
    float rounding, so formant_shift=0 is transparent.

    RT contract (steady state): zero heap allocation, no locks, no exceptions,
    no IO.  All buffers and the FFT are created in prepare(); process() writes
    only into preallocated storage.  JUCE's real-only FFT uses a 16 KB alloca
    scratch at this size (below its 256 KB heap threshold).
*/

#include "dsp/FormantEngine.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dsp
{

namespace
{

constexpr int fftOrder = 11;
constexpr int fftSize  = 1 << fftOrder;          // 2048
constexpr int hopSize  = fftSize / 4;            // 512
constexpr int numBins  = fftSize / 2 + 1;        // 1025

constexpr float olaScale = 2.0f / 3.0f;          // 1 / (4 x periodic-Hann OLA)
constexpr float envelopeContrast = 1.5f;         // T68: formant emphasis
constexpr float bandLowHz  = 150.0f;
constexpr float bandHighHz = 5500.0f;
constexpr float bandXfadeHz = 1000.0f;
constexpr float gainLimit = 7.943282347242816f;  // 10^(18/20)
constexpr float logGainLimit = 2.072326583694641f; // ln(gainLimit)
constexpr float frameSilence = 1.0e-12f;
constexpr float paramSmoothingAlpha = 0.3f;      // per hop (~31 ms @48k)
constexpr float normSmoothingAlpha = 0.3f;       // per hop (~31 ms @48k)

inline std::size_t wrapIndex (std::int64_t value, std::size_t size) noexcept
{
    const auto m = value % static_cast<std::int64_t> (size);
    return static_cast<std::size_t> (m < 0 ? m + static_cast<std::int64_t> (size) : m);
}

float bandWeightForHz (float hz) noexcept
{
    if (hz <= bandLowHz || hz >= bandHighHz)
        return 0.0f;

    if (hz < bandLowHz + bandXfadeHz)
        return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi
                                        * (hz - bandLowHz) / bandXfadeHz);

    if (hz > bandHighHz - bandXfadeHz)
        return 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi
                                        * (hz - (bandHighHz - bandXfadeHz)) / bandXfadeHz);

    return 1.0f;
}

} // namespace

//==============================================================================
struct FormantEngine::Impl
{
    Impl (double newSampleRate, int maxBlockSize)
        : sampleRate (newSampleRate),
          fft (fftOrder),
          window (static_cast<size_t> (fftSize)),
          inputRing (static_cast<size_t> (fftSize + std::max (1, maxBlockSize) + hopSize + 64)),
          olaRing (static_cast<size_t> (fftSize + latencySamples
                                        + std::max (1, maxBlockSize) + hopSize + 64)),
          fftData (static_cast<size_t> (fftSize * 2)),
          envData (static_cast<size_t> (fftSize * 2)),
          frameBuffer (static_cast<size_t> (fftSize)),
          logEnvelope (static_cast<size_t> (numBins)),
          gain (static_cast<size_t> (numBins)),
          bandWeight (static_cast<size_t> (numBins))
    {
        for (int n = 0; n < fftSize; ++n)
            window[static_cast<size_t> (n)] =
                0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                        * static_cast<float> (n) / static_cast<float> (fftSize));

        const auto nyquist = static_cast<float> (sampleRate * 0.5);
        for (int k = 0; k < numBins; ++k)
        {
            const auto hz = static_cast<float> (k) * static_cast<float> (sampleRate)
                            / static_cast<float> (fftSize);
            bandWeight[static_cast<size_t> (k)] =
                hz < nyquist ? bandWeightForHz (hz) : 0.0f;
        }

        warmup();
    }

    //==========================================================================
    void warmup() noexcept
    {
        // Exercise every FFT branch once so no lazy setup can happen on the
        // audio thread.  All memory is already owned by the members.
        std::fill (inputRing.begin(), inputRing.end(), 0.001f);
        currentShift = 1.0f;          // exercise the warp path
        currentQuefrency = 1.0f;      // exercise the cepstral/envelope path
        writePos = static_cast<std::int64_t> (fftSize);

        for (int i = 0; i < 4; ++i)
        {
            processOneFrame();
            writePos += hopSize;
        }

        resetStreaming();
    }

    void resetStreaming() noexcept
    {
        std::fill (inputRing.begin(), inputRing.end(), 0.0f);
        std::fill (olaRing.begin(), olaRing.end(), 0.0f);
        writePos = 0;
        samplesAccumulated = 0;
        normGain = 1.0f;
        currentShift = targetShift.load (std::memory_order_relaxed);
        currentQuefrency = targetQuefrency.load (std::memory_order_relaxed);
    }

    //==========================================================================
    void setShift (float semitones) noexcept
    {
        targetShift.store (std::isnan (semitones) ? 0.0f
                                                  : std::clamp (semitones, -12.0f, 12.0f),
                           std::memory_order_relaxed);
    }

    void setQuefrency (float milliseconds) noexcept
    {
        targetQuefrency.store (std::isnan (milliseconds) ? 0.0f
                                                         : std::clamp (milliseconds, 0.0f, 10.0f),
                               std::memory_order_relaxed);
    }

    //==========================================================================
    void process (const float* input, float* output, int numSamples) noexcept
    {
        for (int n = 0; n < numSamples; ++n)
        {
            inputRing[wrapIndex (writePos, inputRing.size())] = input[n];
            ++writePos;

            if (++samplesAccumulated >= hopSize)
            {
                samplesAccumulated = 0;
                processOneFrame();
            }

            const auto readAbs = writePos - static_cast<std::int64_t> (latencySamples) - 1;
            // Before the first full latency window there is no real output;
            // reading the wrapped ring here would alias pre-stream OLA taps.
            if (readAbs >= 0)
            {
                const auto readIndex = wrapIndex (readAbs, olaRing.size());
                output[n] = olaRing[readIndex] * olaScale;
                // The OLA accumulator is consumed once per absolute position;
                // clearing it keeps a later ring wrap from replaying old taps.
                olaRing[readIndex] = 0.0f;
            }
            else
            {
                output[n] = 0.0f;
            }
        }
    }

private:
    //==========================================================================
    void processOneFrame() noexcept
    {
        currentShift += (targetShift.load (std::memory_order_relaxed) - currentShift)
                        * paramSmoothingAlpha;
        currentQuefrency += (targetQuefrency.load (std::memory_order_relaxed) - currentQuefrency)
                            * paramSmoothingAlpha;

        // --- windowed input frame: the newest fftSize samples -------------
        float frameEnergy = 0.0f;
        for (int j = 0; j < fftSize; ++j)
        {
            const auto absIndex = writePos - fftSize + j;
            const auto x = inputRing[wrapIndex (absIndex, inputRing.size())];
            frameBuffer[static_cast<size_t> (j)] = x * window[static_cast<size_t> (j)];
            frameEnergy += x * x;
        }

        if (frameEnergy < frameSilence)
            return; // silence contributes nothing to the OLA

        // --- forward FFT ---------------------------------------------------
        std::copy (frameBuffer.begin(), frameBuffer.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performRealOnlyForwardTransform (fftData.data(), false);

        const auto ratio = std::pow (2.0f, currentShift / 12.0f);
        const auto qSamples = currentQuefrency <= 0.0f
                                ? 0
                                : std::clamp (static_cast<int> (std::lround (
                                                   static_cast<double> (currentQuefrency) * 1.0e-3 * sampleRate)),
                                              2, numBins - 2);

        // --- cepstral envelope + 1.5x contrast -----------------------------
        if (qSamples > 0)
        {
            constexpr float logFloor = 1.0e-12f;
            for (int k = 0; k < numBins; ++k)
            {
                const auto re = fftData[static_cast<size_t> (2 * k)];
                const auto im = fftData[static_cast<size_t> (2 * k + 1)];
                const auto mag = std::sqrt (re * re + im * im);
                envData[static_cast<size_t> (2 * k)] = std::log (std::max (mag, logFloor));
                envData[static_cast<size_t> (2 * k + 1)] = 0.0f;
            }

            fft.performRealOnlyInverseTransform (envData.data());

            // Symmetric lifter: keep |quefrency| <= q, zero the rest.
            for (int n = qSamples + 1; n < fftSize - qSamples; ++n)
                envData[static_cast<size_t> (n)] = 0.0f;

            std::fill (envData.begin() + fftSize, envData.end(), 0.0f);
            fft.performRealOnlyForwardTransform (envData.data(), false);

            double mean = 0.0;
            for (int k = 0; k < numBins; ++k)
                mean += static_cast<double> (envData[static_cast<size_t> (2 * k)]);
            const auto avg = static_cast<float> (mean / static_cast<double> (numBins));

            for (int k = 0; k < numBins; ++k)
            {
                const auto le = envData[static_cast<size_t> (2 * k)];
                logEnvelope[static_cast<size_t> (k)] =
                    avg + envelopeContrast * (le - avg);
            }
        }
        else
        {
            std::fill (logEnvelope.begin(), logEnvelope.end(), 0.0f);
        }

        // --- gain curve + frame energy -------------------------------------
        double inEnergy = 0.0, outEnergy = 0.0;
        for (int k = 0; k < numBins; ++k)
        {
            const auto re = fftData[static_cast<size_t> (2 * k)];
            const auto im = fftData[static_cast<size_t> (2 * k + 1)];
            const auto mag2 = static_cast<double> (re) * re + static_cast<double> (im) * im;
            inEnergy += mag2;

            float g = 1.0f;
            if (qSamples > 0)
            {
                auto srcBin = static_cast<float> (k) / ratio;
                srcBin = std::min (srcBin, static_cast<float> (numBins - 1));
                const auto i0 = static_cast<int> (srcBin);
                const auto i1 = std::min (i0 + 1, numBins - 1);
                const auto t = srcBin - static_cast<float> (i0);
                const auto le = logEnvelope[static_cast<size_t> (i0)] * (1.0f - t)
                                + logEnvelope[static_cast<size_t> (i1)] * t;
                const auto diff = std::clamp (le - logEnvelope[static_cast<size_t> (k)],
                                              -logGainLimit, logGainLimit);
                g = std::exp (diff);
            }

            g = 1.0f + (g - 1.0f) * bandWeight[static_cast<size_t> (k)];
            g = std::clamp (g, 1.0f / gainLimit, gainLimit);
            gain[static_cast<size_t> (k)] = g;
            outEnergy += mag2 * static_cast<double> (g) * g;
        }

        const auto targetNorm = static_cast<float> (std::sqrt (
            inEnergy / std::max (outEnergy, 1.0e-30)));
        normGain += (targetNorm - normGain) * normSmoothingAlpha;
        const auto totalGain = normGain;

        for (int k = 0; k < numBins; ++k)
        {
            const auto g = gain[static_cast<size_t> (k)] * totalGain;
            fftData[static_cast<size_t> (2 * k)] *= g;
            fftData[static_cast<size_t> (2 * k + 1)] *= g;
        }

        // --- inverse + OLA (original phase is preserved) --------------------
        fft.performRealOnlyInverseTransform (fftData.data());

        const auto frameStart = writePos - fftSize;
        for (int j = 0; j < fftSize; ++j)
        {
            // Negative absolute positions are the pre-stream history (zeros);
            // skipping them keeps the wrapped ring free of startup taps.
            const auto absIndex = frameStart + j;
            if (absIndex < 0)
                continue;

            auto& dst = olaRing[wrapIndex (absIndex, olaRing.size())];
            dst += fftData[static_cast<size_t> (j)] * window[static_cast<size_t> (j)];
        }
    }

    //==========================================================================
    double sampleRate;
    juce::dsp::FFT fft;

    std::vector<float> window;
    std::vector<float> inputRing;
    std::vector<float> olaRing;
    std::vector<float> fftData;
    std::vector<float> envData;
    std::vector<float> frameBuffer;
    std::vector<float> logEnvelope;
    std::vector<float> gain;
    std::vector<float> bandWeight;

    std::int64_t writePos = 0;
    int samplesAccumulated = 0;
    float normGain = 1.0f;

    std::atomic<float> targetShift { 0.0f };
    float currentShift = 0.0f;
    std::atomic<float> targetQuefrency { 0.0f };
    float currentQuefrency = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Impl)
};

//==============================================================================
FormantEngine::~FormantEngine() = default;

void FormantEngine::prepare (double sampleRate, int maxBlockSize) noexcept
{
    impl = std::make_shared<Impl> (sampleRate, std::max (1, maxBlockSize));
}

void FormantEngine::setShiftSemitones (float semitones) noexcept
{
    if (impl != nullptr)
        impl->setShift (semitones);
}

void FormantEngine::setQuefrencyMs (float milliseconds) noexcept
{
    if (impl != nullptr)
        impl->setQuefrency (milliseconds);
}

void FormantEngine::clear() noexcept
{
    if (impl != nullptr)
        impl->resetStreaming();
}

void FormantEngine::process (const float* input, float* output, int numSamples) noexcept
{
    if (impl != nullptr)
        impl->process (input, output, numSamples);
}

} // namespace dsp
