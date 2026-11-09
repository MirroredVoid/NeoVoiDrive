#pragma once

/*
    HarmonicExciter: drive + tone-filtered soft saturation (T09, DECISIONS §3.2).

        wet = toneFilter (tanh (x_up * preGain))     @ 2x sample rate
        y   = dry (latency-matched) * (1 - drive) + wet * drive

      - 2x oversampling with a polyphase IIR half-band (DECISIONS §3.2, A6):
        tanh harmonics above the output Nyquist are filtered before
        downsampling, so aliased images stay below -40 dB (T09 acceptance,
        quantified: without oversampling a 7 kHz input at drive=100% folded
        the 5th harmonic back to 13 kHz at -14.5 dB);
      - the dry path is delayed by the oversampler latency so the dry/wet mix
        never comb-filters;
      - drive == 0 -> y == x exactly (bit-transparent) AND the whole wet path
        (oversampler + tanh + tone filter) is skipped (R9: no oversampling CPU
        is spent; the frozen state resumes seamlessly as drive ramps up);
      - preGain ramps 1..10 as drive goes 0..100% (task acceptance: "驱动
        1→10"), feeding the tanh soft clipper;
      - toneFilter: RBJ constant-peak-gain 2-pole bandpass (Q = 1), peak at
        `tone` Hz (SPEC §4.3 harmonic_tone 1000-10000, log), running at 2x;
      - drive smoothed with a 10 ms ramp, tone with 20 ms (SPEC "平滑" column).

    Group delay: oversampler latency (1-2 samples at 48 kHz) + tone filter
    group delay, well below the 10-sample budget (R11, T09 acceptance).

    RT contract: zero allocation, no locks, no exceptions.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

#include "../util/SmoothedValue.h"

namespace dsp
{

class HarmonicExciter
{
public:
    // SPEC §4.3 smoothing column.
    static constexpr float driveSmoothingMilliseconds = 10.0f;
    static constexpr float toneSmoothingMilliseconds  = 20.0f;

    // RBJ bandpass Q (tone-shaping resonance).
    static constexpr float filterQ = 1.0f;

    // DECISIONS §3.2: 2x oversampling, polyphase IIR half-band, integer latency.
    static constexpr int oversamplingFactor = 2;

    /** Per-block parameter bundle (subset of params::ParamSnapshot::Band). */
    struct Params
    {
        float drive = 0.0f;   // % (SPEC harmonic_drive 0-100)
        float tone  = 3000.0f; // Hz (SPEC harmonic_tone 1000-10000, log)
    };

    /** Configures sample rate, oversampler and resets state. */
    void prepare (double newSampleRate, int maxBlockSize) noexcept
    {
        sampleRate = newSampleRate;

        // Custom polyphase-IIR half-band: the stock max-quality defaults
        // (down: tw=0.06, -75 dB) only give ~25 dB of aliasing rejection at
        // the worst case (7 kHz input, drive=100%); tighten the downsampler
        // so folded images stay below the -40 dB acceptance (T09).  (Very
        // tight specs like tw=0.02/-110 dB make the IIR design non-converge.)
        // NOTE: clear/add must come BEFORE initProcessing, which allocates the
        // stage buffers.
        oversampler.clearOversamplingStages();
        oversampler.addOversamplingStage (juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                          0.05f, -90.0f,  // upsampler
                                          0.04f, -90.0f); // downsampler
        oversampler.initProcessing (static_cast<size_t> (juce::jmax (1, maxBlockSize)));
        oversampler.reset();

        dryLatencySamples = juce::jmax (1, static_cast<int> (oversampler.getLatencyInSamples()));
        // Release builds have no jassert: clamp to the ring so a future JUCE
        // latency change can only misalign the dry path, never index garbage.
        dryLatencySamples = juce::jmin (dryLatencySamples, static_cast<int> (dryBufferSize) - 1);
        jassert (static_cast<int> (oversampler.getLatencyInSamples()) < static_cast<int> (dryBufferSize));
        dryBuffer.fill (0.0f);
        dryPos = 0;

        driveSmooth.prepare (sampleRate, driveSmoothingMilliseconds);
        toneSmooth.prepare (sampleRate, toneSmoothingMilliseconds);
        driveSmooth.reset (0.0f);
        toneSmooth.reset (3000.0f);
        coeffStale = true;
        toneCoeffCountdown = 0;
        toneCoeffUpdates = 0;
        resetFilter();
    }

    /** Zeros the filter state (e.g. on transport stop). */
    void resetFilter() noexcept
    {
        x1 = x2 = y1 = y2 = 0.0;
    }

    /** Clears the oversampler and the latency-matched dry ring (structural
        routing changes). No reallocation. */
    void resetState() noexcept
    {
        oversampler.reset();
        resetFilter();
        dryBuffer.fill (0.0f);
        dryPos = 0;
    }

    /** Applies the per-block parameters. RT safe; both values are smoothed
        internally. Drive is clamped to [0, 100]. */
    void setParameters (const Params& p) noexcept
    {
        driveSmooth.setTarget (juce::jlimit (0.0f, 100.0f, p.drive));
        toneSmooth.setTarget (juce::jlimit (500.0f, 20000.0f, p.tone));
    }

    /** Advances one sample. */
    float process (float x) noexcept
    {
        const auto drive = driveSmooth.getNext() * 0.01f; // % -> 0..1
        const auto tone  = toneSmooth.getNext();          // Hz

        // Latency-matched dry path (kept advancing even at zero drive so the
        // dry/wet mix is always aligned when drive returns).
        // The ring must be strictly larger than the oversampler latency
        // (measured ~4-5 for 2x); guard in prepare().
        dryBuffer[static_cast<size_t> (dryPos)] = x;
        const auto dry = dryBuffer[static_cast<size_t> ((dryPos + dryBufferSize
                                                         - dryLatencySamples) % dryBufferSize)];
        dryPos = (dryPos + 1) % dryBufferSize;

        // R9: at zero drive skip the entire wet path — no oversampling CPU.
        // M2 (T13 review, user decision): still return the latency-matched
        // dry so adjacent bands stay phase-aligned at the crossover (LR flat
        // rebuild); the fixed ~7-sample delay is reported as 0 per the
        // SPEC §5.1 <10-sample compromise (DECISIONS). The frozen oversampler
        // state resumes seamlessly when drive returns.
        if (drive == 0.0f)
        {
            // The tone smoother keeps moving while the wet path is idle; the
            // coefficients must be refreshed before the next active sample.
            coeffStale = true;
            return dry;
        }

        // ---- upsample (2x) ----
        float inData[1] = { x };
        const float* inChannels[1] = { inData };
        const juce::dsp::AudioBlock<const float> inBlock (inChannels, 1, 1);
        auto up = oversampler.processSamplesUp (inBlock);
        auto* upPtr = up.getChannelPointer (0);

        // ---- tone filter coefficients (RBJ constant-peak-gain bandpass,
        //      evaluated at 2x sample rate) ----
        // Audit P3: the 20 ms-smoothed tone does not need a cos/sin pair per
        // sample. Refresh at a control rate (32 input samples ≈ 0.67 ms at
        // 48 kHz); a drive return after the zero-drive path forces a refresh.
        if (coeffStale || --toneCoeffCountdown <= 0)
        {
            updateToneCoefficients (tone);
            coeffStale = false;
            toneCoeffCountdown = toneCoeffUpdateSamples;
            ++toneCoeffUpdates;
        }

        // ---- wet path @2x: soft clip -> tone filter, processed IN-PLACE on
        //      the oversampler buffer (processSamplesDown reads that buffer,
        //      so the nonlinear result must live there) ----
        const auto preGain = 1.0f + drive * 9.0f; // 1..10 (task acceptance)
        const auto s0 = upPtr[0];
        const auto s1 = upPtr[1];
        upPtr[0] = runToneBiquad (std::tanh (s0 * preGain), coeffB0, coeffB2, coeffA1, coeffA2);
        upPtr[1] = runToneBiquad (std::tanh (s1 * preGain), coeffB0, coeffB2, coeffA1, coeffA2);

        // ---- downsample (writes the filtered result into outData) ----
        float outData[1];
        float* outChannels[1] = { outData };
        juce::dsp::AudioBlock<float> outBlock (outChannels, 1, 1);
        oversampler.processSamplesDown (outBlock);
        const auto wet = outData[0];

        // ---- dry/wet crossfade (bit-transparent at drive == 0) ----
        return dry * (1.0f - drive) + wet * drive;
    }

    /** Current smoothed drive in % (diagnostics / tests). */
    float getSmoothedDrive() const noexcept
    {
        return driveSmooth.getCurrentValue();
    }

    /** Current smoothed tone in Hz (diagnostics / tests). */
    float getSmoothedTone() const noexcept
    {
        return toneSmooth.getCurrentValue();
    }

    /** Oversampler latency in output samples (diagnostics / tests). */
    int getLatencySamples() const noexcept
    {
        return dryLatencySamples;
    }

    /** Control-rate tone-coefficient refreshes (diagnostics / tests). */
    int getToneCoefficientUpdates() const noexcept
    {
        return toneCoeffUpdates;
    }

private:
    static constexpr size_t dryBufferSize = 8;
    // Audit P3: 20 ms-smoothed tone -> cos/sin at ~1.5 kHz, not per sample.
    static constexpr int toneCoeffUpdateSamples = 32;

    void updateToneCoefficients (float tone) noexcept
    {
        auto omega = static_cast<float> (juce::MathConstants<double>::twoPi
                                         * tone / (2.0 * sampleRate));
        if (omega > 2.8f) // ~= 0.9 * pi: keep the biquad stable at low rates
            omega = 2.8f;

        const auto cosW = std::cos (omega);
        const auto alpha = std::sin (omega) / (2.0f * filterQ);
        const auto a0 = 1.0f + alpha;
        coeffB0 = alpha / a0;
        coeffB2 = -alpha / a0;
        coeffA1 = (-2.0f * cosW) / a0;
        coeffA2 = (1.0f - alpha) / a0;
    }

    float runToneBiquad (float x, double b0, double b2, double a1, double a2) noexcept
    {
        const auto y = b0 * static_cast<double> (x) + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return static_cast<float> (y);
    }

    //==========================================================================
    double sampleRate = 48000.0;
    juce::dsp::Oversampling<float> oversampler { 1, oversamplingFactor,
                                                 juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                 true, true }; // max quality, integer latency

    int dryLatencySamples = 1;
    std::array<float, dryBufferSize> dryBuffer {};
    int dryPos = 0;

    util::SmoothedValue driveSmooth;
    util::SmoothedValue toneSmooth;
    float coeffB0 = 0.0f, coeffB2 = 0.0f, coeffA1 = 0.0f, coeffA2 = 0.0f;
    int toneCoeffCountdown = 0;
    bool coeffStale = true;
    int toneCoeffUpdates = 0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0; // tone biquad state (R7)
};

} // namespace dsp
