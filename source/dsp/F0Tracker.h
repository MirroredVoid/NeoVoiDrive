#pragma once

/*
    F0Tracker — McLeod Pitch Method (MPM / NSDF) fundamental frequency tracker
    (T27, SIDEBAND_SPEC §6/§7).

    Pipeline per hop (≈10 ms, control rate only):
        ring buffer → NSDF(τ) via FFT-accelerated autocorrelation (complex
        4096-point FFT, 2× window) → first significant peak + parabolic
        interpolation → raw f0 → median-3 → one-pole lowpass (≈15 ms,
        DECISIONS R24) → slope limit (60 st/hop ≈ 6000 st/s, DECISIONS
        R23/R25) → f0_smooth.

    Voiced/hold/unvoiced state machine (SIDEBAND_SPEC §7): clarity hysteresis
    (voiced ≥ 0.6 / unvoiced < 0.45), 600 ms hold timeout freezing f0_smooth.
    T36 rev24: hold 200 → 600 ms — short source weak segments (the periodic
    dips of test tones like 0.mp3) no longer drop the wet path; its 100 ms
    release was the fixed-position "pump" heard across rev6–rev23 (dips sat
    at the SOURCE's weak segments, scaled by roughness, never at the
    modulator).

    Control-signal only: the ≈W/2 + hop ≈ 31 ms analysis lag never touches the
    audio path (SIDEBAND_SPEC §6.4). Audio path latency is unaffected.

    RT contract: pushSample/runNsdf are zero heap allocation, no exceptions,
    no blocking. All buffers are allocated in prepare(). The JUCE FFT uses an
    internal spin lock, but it is only ever touched from one thread here
    (uncontended, same as FormantEngine).

    The NSDF normalisation is self-calibrating against any FFT scale factor:
        NSDF(τ) = 2·r_fft(τ)·m0 / (r_fft(0)·(m0 + mτ)),  mτ = Σ_{j=τ}^{W-1} x[j]²
    so NSDF(0) ≡ 1 regardless of the engine's 1/N convention.
*/

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace dsp
{

class F0Tracker
{
public:
    enum class State { Unvoiced, Hold, Voiced };

    // SIDEBAND_SPEC §6.2 (fixed window; τ range and hop scale with sample rate)
    static constexpr int windowSize = 2048;
    static constexpr int fftOrder   = 12;            // 4096-point complex FFT
    static constexpr int fftSize    = 1 << fftOrder;

    // Clarity hysteresis and hold timeout (SIDEBAND_SPEC §6.2 / §7; T36
    // rev24: 600 ms hold keeps the wet path alive across short weak segments)
    static constexpr float voicedThreshold     = 0.60f;
    static constexpr float unvoicedThreshold   = 0.45f;
    static constexpr float maxHoldSeconds      = 0.600f;
    static constexpr double lowpassSeconds     = 0.015;          // §6.3 (R24 ruling: 50→15 ms)
    static constexpr float maxSlopeSemitonesPerHop = 60.0f;     // DECISIONS R23/R25

    /** Allocates the ring buffer, FFT and work areas. Not RT (prepare only). */
    void prepare (double newSampleRate, int maxBlockSize) noexcept
    {
        sampleRate = newSampleRate;

        hopSize = juce::jmax (1, static_cast<int> (std::lround (sampleRate * 0.01)));      // hop ≈ 10 ms
        tauMin  = juce::jmax (2, static_cast<int> (std::lround (sampleRate / 1000.0)));    // f0 ≤ 1000 Hz
        tauMax  = juce::jmin (windowSize - 2, static_cast<int> (std::lround (sampleRate / 100.0))); // f0 ≥ 100 Hz
        if (tauMax <= tauMin + 1)
            tauMax = tauMin + 1;

        ringSize   = windowSize + juce::jmax (maxBlockSize, 1);   // SIDEBAND_SPEC §11
        ring.assign (static_cast<size_t> (ringSize), 0.0f);
        fftA.assign (static_cast<size_t> (fftSize), juce::dsp::Complex<float>());
        fftB.assign (static_cast<size_t> (fftSize), juce::dsp::Complex<float>());
        prefixSq.assign (static_cast<size_t> (windowSize + 1), 0.0f);
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

        reset();
    }

    /** T29 sleep control: inactive = ring keeps advancing, NSDF skipped. */
    void setActive (bool on) noexcept { active = on; }

    /** One input sample (audio thread). Runs the NSDF at the hop cadence. */
    void pushSample (float x) noexcept
    {
        ring[writePos] = x;
        writePos = (writePos + 1) % ringSize;

        if (active && ++samplesInHop >= hopSize)
        {
            samplesInHop = 0;
            runNsdf();
        }
    }

    void reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        samplesInHop = 0;
        medianCount  = 0;
        f0Estimate = 0.0f;
        f0Lowpass  = 0.0f;
        f0Smooth   = 0.0f;
        clarity    = 0.0f;
        state      = State::Unvoiced;   // §7: startup state = unvoiced
        holdSeconds = 0.0f;
    }

    //==========================================================================
    //  Diagnostics (tests / T29 integration)
    //==========================================================================
    /** Smoothed f0 in Hz; 0 while unvoiced (frozen during hold). */
    float getF0()      const noexcept { return f0Smooth; }
    /** NSDF peak clarity of the most recent hop (0..1). */
    float getClarity() const noexcept { return clarity; }
    /** Voiced or hold (i.e. the engine should keep the wet path running). */
    bool  isVoiced()   const noexcept { return state != State::Unvoiced; }
    State getState()   const noexcept { return state; }
    /** Number of NSDF executions (T29 sleep / P02 audit assertions). */
    int   getNsdfRuns() const noexcept { return nsdfRuns; }

    /** Test-only: drives the state machine with a synthetic clarity value.
        A real signal cannot reliably land inside the narrow hysteresis band,
        and the hold timing must be deterministic. */
    void updateStateForTest (float syntheticClarity, float rawF0 = 200.0f) noexcept
    {
        clarity = syntheticClarity;
        updateState (rawF0);
    }

private:
    float nsdfAt (int tau, float scale) const noexcept
    {
        // MPM NSDF with overlap-window normalisation (raw MPM definition):
        //   m'(τ) = Σ_{j=0}^{W-1-τ} x[j]²   (energy of the FIRST sequence's overlap)
        //   mτ    = Σ_{j=τ}^{W-1}   x[j]²   (energy of the SECOND sequence's overlap)
        //   NSDF(τ) = 2·r(τ) / (m'(τ) + mτ)      →  NSDF(period) ≡ 1 for a pure tone
        // Self-calibrating scale: 2·m0 / r_fft(0), m0 = Σ x² over the whole window.
        const float mPrime = prefixSq[0] - prefixSq[windowSize - tau];
        const float mTau   = prefixSq[tau];
        return scale * fftA[static_cast<size_t> (tau)].real() / (mPrime + mTau);
    }

    void runNsdf() noexcept
    {
        ++nsdfRuns;

        // Copy the most recent window into the FFT input buffer (wrap-aware).
        const int start = writePos - windowSize;
        const int s = start < 0 ? start + ringSize : start;
        for (int i = 0; i < windowSize; ++i)
            fftA[static_cast<size_t> (i)] = { ring[static_cast<size_t> ((s + i) % ringSize)], 0.0f };
        for (int i = windowSize; i < fftSize; ++i)
            fftA[static_cast<size_t> (i)] = { 0.0f, 0.0f };

        // mτ = Σ_{j=τ}^{W-1} x[j]² as a suffix sum over the window.
        double sq = 0.0;
        for (int i = windowSize - 1; i >= 0; --i)
        {
            const float v = fftA[static_cast<size_t> (i)].real();
            sq += static_cast<double> (v) * v;
            prefixSq[static_cast<size_t> (i)] = static_cast<float> (sq);
        }
        prefixSq[static_cast<size_t> (windowSize)] = 0.0f;

        // Autocorrelation via |FFT(x)|² → IFFT. JUCE's dsp::FFT::perform is
        // OUT-OF-PLACE only (the fallback engine's recursive radix steps read
        // and write different pointers), so two buffers are required.
        fft->perform (fftA.data(), fftB.data(), false);
        for (int i = 0; i < fftSize; ++i)
            fftB[static_cast<size_t> (i)] = fftB[static_cast<size_t> (i)]
                                          * std::conj (fftB[static_cast<size_t> (i)]);
        fft->perform (fftB.data(), fftA.data(), true);

        const float m0 = prefixSq[0];
        const float r0 = fftA[0].real(); // = k·m0 for some scale factor k
        if (! (m0 > 1e-12f) || ! (std::abs (r0) > 1e-12f))
        {
            clarity = 0.0f;          // silence → unvoiced
            updateState (0.0f);
            return;
        }

        const float scale = 2.0f * m0 / r0;

        // Peak picking (MPM): find the strongest local peak in [tauMin, tauMax],
        // then the FIRST peak reaching 90% of it — that is the fundamental
        // period (octave-error guard, SIDEBAND_SPEC §6.1). The scan is
        // inclusive of BOTH ends: f0 = 1000 Hz puts the peak exactly at
        // tauMin, f0 = 100 Hz exactly at tauMax.
        float bestNsdf = 0.0f;
        int   bestTau  = -1;
        float prev = nsdfAt (tauMin - 1, scale);
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            const float cur  = nsdfAt (tau, scale);
            const float next = nsdfAt (tau + 1, scale);
            if (cur > prev && cur > next && cur > bestNsdf)
            {
                bestNsdf = cur;
                bestTau  = tau;
            }
            prev = cur;
        }

        if (bestTau < 0)
        {
            clarity = 0.0f;
            updateState (0.0f);
            return;
        }

        const float firstPeakThreshold = bestNsdf * 0.9f;
        float firstNsdf = 0.0f;
        int   firstTau  = -1;
        prev = nsdfAt (tauMin - 1, scale);
        for (int tau = tauMin; tau <= bestTau; ++tau)
        {
            const float cur  = nsdfAt (tau, scale);
            const float next = nsdfAt (tau + 1, scale);
            if (cur > prev && cur > next && cur >= firstPeakThreshold)
            {
                firstNsdf = cur;
                firstTau  = tau;
                break;
            }
            prev = cur;
        }

        if (firstTau < 0)
        {
            firstTau  = bestTau;
            firstNsdf = bestNsdf;
        }

        // Parabolic interpolation to a sub-sample lag.
        const float ym = nsdfAt (firstTau - 1, scale);
        const float y0 = firstNsdf;
        const float yp = nsdfAt (firstTau + 1, scale);
        const float denom = ym - 2.0f * y0 + yp;
        const float delta = std::abs (denom) > 1e-9f ? 0.5f * (ym - yp) / denom : 0.0f;
        const float tauSub = static_cast<float> (firstTau) + delta;

        clarity = firstNsdf;
        const float rawF0 = static_cast<float> (sampleRate / static_cast<double> (juce::jmax (tauSub, 1.0f)));

        updateState (rawF0);
    }

    void updateState (float rawF0) noexcept
    {
        if (clarity >= voicedThreshold)
        {
            // voiced: (re)enter tracking, trajectory smooths from the frozen
            // value so hold→voiced never jumps (SIDEBAND_SPEC §7).
            holdSeconds = 0.0f;
            state = State::Voiced;
            updateTrajectory (rawF0);
            return;
        }

        // Not confidently voiced. The [unvoicedThreshold, voicedThreshold)
        // hysteresis band still blocks re-locking (no trajectory update), but
        // it must not extend a Hold indefinitely: the timer runs from the
        // moment the state became uncertain, regardless of which side of the
        // band the clarity sits (audit P2-4; SIDEBAND_SPEC §7 caps hold at
        // 600 ms).
        if (state == State::Voiced)
            state = State::Hold;

        if (state == State::Hold)
        {
            holdSeconds += static_cast<float> (hopSize) / static_cast<float> (sampleRate);
            if (holdSeconds >= maxHoldSeconds)
            {
                state = State::Unvoiced;         // §7: hold timeout
                f0Smooth = 0.0f;
                f0Lowpass = 0.0f;                // reset the trajectory: a later
                medianCount = 0;                 // re-lock must NOT inherit the
                holdSeconds = 0.0f;              // pre-gap f0 (fast re-lock, SB-U04)
            }
        }
        // else: already unvoiced — stay (getF0 stays 0, "frozen")
    }

    void updateTrajectory (float rawF0) noexcept
    {
        f0Estimate = rawF0;

        // Median-3 (octave-jump guard, §6.3).
        medianBuf[medianCount % 3] = rawF0;
        ++medianCount;
        float m;
        if (medianCount >= 3)
        {
            float a = medianBuf[0], b = medianBuf[1], c = medianBuf[2];
            if (a > b) std::swap (a, b);
            if (b > c) { std::swap (b, c); if (a > b) std::swap (a, b); }
            m = b;
        }
        else
        {
            m = rawF0; // warm-up: feed through
        }

        // One-pole lowpass ≈ 15 ms (hop domain, §6.3; DECISIONS R24).
        const float alpha = 1.0f - std::exp (-static_cast<float> (hopSize)
                            / (static_cast<float> (sampleRate) * static_cast<float> (lowpassSeconds)));
        if (f0Lowpass <= 0.0f)
            f0Lowpass = m;
        else
            f0Lowpass += alpha * (m - f0Lowpass);

        // Slope limit: ≤ 60 st per hop (≈6000 st/s, DECISIONS R23/R25). First
        // lock jumps straight in (no previous value to limit against).
        if (f0Smooth <= 0.0f)
        {
            f0Smooth = f0Lowpass;
        }
        else
        {
            const float st = juce::jlimit (-maxSlopeSemitonesPerHop, maxSlopeSemitonesPerHop,
                                           1200.0f * std::log2 (f0Lowpass / f0Smooth));
            f0Smooth *= std::pow (2.0f, st / 1200.0f);
        }
    }

    //==========================================================================
    double sampleRate = 48000.0;
    int hopSize = 480;
    int tauMin = 48;
    int tauMax = 480;

    std::vector<float> ring;
    int ringSize = 0;
    int writePos = 0;
    int samplesInHop = 0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<juce::dsp::Complex<float>> fftA;   // window / autocorrelation out
    std::vector<juce::dsp::Complex<float>> fftB;   // spectrum work area
    std::vector<float> prefixSq;   // suffix sums mτ

    float f0Estimate = 0.0f;
    float medianBuf[3] = {};
    int   medianCount = 0;
    float f0Lowpass = 0.0f;
    float f0Smooth  = 0.0f;
    float clarity   = 0.0f;

    State state = State::Unvoiced;
    float holdSeconds = 0.0f;
    bool  active = true;
    int   nsdfRuns = 0;
};

} // namespace dsp
