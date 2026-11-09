#pragma once

/*
    GrowlEngine (T66) — global F0-tracked “growl” texture.

    Accepted N3 behaviour:
      - amplitude modulation at 0.20 × tracked F0 (user-adjustable ratio),
        m = 0.45 · amount, second-order term 0.12, 8 % band-limited noise AM;
      - random FM jitter (≈7 Hz) and depth variation (≈5 Hz);
      - clarity/voiced gate: unvoiced material passes through untouched;
      - bipolar tone EQ: tone 0 = N3 (‑3 dB @4 kHz Q1.2 + ‑1 dB @8 kHz),
        negative = brighter (towards flat), positive = darker (up to ‑6/‑2 dB);
      - amount = 0 is bit-transparent (no AM, no EQ).

    The engine is shared by L/R (one control signal); the EQ has independent
    L/R state. It is header-only and allocation-free after prepare().

    No delay line: the effect adds no samples of latency and does not change
    the reported PDC.
*/

#include "util/SmoothedValue.h"
#include "RandomModSource.h"
#include "TrigApprox.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dsp
{

class GrowlEngine
{
public:
    struct Params
    {
        float amount = 0.0f;  // %
        float rate   = 0.20f; // fm / f0
        float tone   = 0.0f;  // −100..+100
    };

    static constexpr float smoothingMilliseconds = 20.0f;
    static constexpr float gateAttackMs          = 20.0f;
    static constexpr float gateReleaseMs         = 40.0f;

    // N3 constants
    static constexpr float baseDepth       = 0.45f;
    static constexpr float secondOrder     = 0.12f;
    static constexpr float noiseDepth      = 0.08f;
    static constexpr float depthVariation  = 0.25f;
    static constexpr float fmJitter        = 0.08f;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = juce::jmax (1.0, newSampleRate);

        amountSmooth.prepare (sampleRate, smoothingMilliseconds);
        rateSmooth.prepare (sampleRate, smoothingMilliseconds);
        toneSmooth.prepare (sampleRate, smoothingMilliseconds);

        jitterSource.prepare (sampleRate);
        jitterSource.setContinuous (true);
        jitterSource.setRate (7.0f);

        depthSource.prepare (sampleRate);
        depthSource.setContinuous (true);
        depthSource.setRate (5.0f);

        noiseSource.prepare (sampleRate);
        noiseSource.setContinuous (true);
        noiseSource.setRate (100.0f); // 60–100 Hz after the 60 Hz high-pass

        // 60 Hz one-pole high-pass for the noise AM control.
        noiseLpAlpha = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 60.0f
                                        / static_cast<float> (sampleRate));

        gateAttackCoeff  = 1.0f - std::exp (-1.0f
                             / (0.001f * gateAttackMs * static_cast<float> (sampleRate)));
        gateReleaseCoeff = 1.0f - std::exp (-1.0f
                             / (0.001f * gateReleaseMs * static_cast<float> (sampleRate)));

        reset();
    }

    /** Once per block (audio thread). NaN-safe like every other DSP unit. */
    void setParameters (const Params& p) noexcept
    {
        const float amount = std::isfinite (p.amount) ? juce::jlimit (0.0f, 100.0f, p.amount) : 0.0f;
        const float rate   = std::isfinite (p.rate)   ? juce::jlimit (0.15f, 0.30f, p.rate) : 0.20f;
        const float tone   = std::isfinite (p.tone)   ? juce::jlimit (-100.0f, 100.0f, p.tone) : 0.0f;

        amountSmooth.setTarget (amount);
        rateSmooth.setTarget (rate);
        toneSmooth.setTarget (tone);

        updateEqCoefficients();
    }

    void setSeed (std::int64_t seed) noexcept
    {
        jitterSource.setSeed (seed ^ 0x4A49545445524C4CLL);
        depthSource.setSeed  (seed ^ 0x44455054484D4F44LL);
        noiseSource.setSeed  (seed ^ 0x4E4F495345414D31LL);
    }

    /** Clears phases, gate and EQ state. Seeds are kept. */
    void reset() noexcept
    {
        amountSmooth.reset (0.0f);
        rateSmooth.reset (0.20f);
        toneSmooth.reset (0.0f);

        phase = 0.0f;
        gate = 0.0f;
        lastGain = 1.0f;
        lastEffective = 0.0f;
        lastF0 = 110.0f;
        lastAmRate = 0.0f;
        noiseLp = 0.0f;

        for (auto& f : peakEq) f.reset();
        for (auto& f : shelfEq) f.reset();
        setIdentity (peakEq[0]); setIdentity (peakEq[1]);
        setIdentity (shelfEq[0]); setIdentity (shelfEq[1]);
        eqPeakDb = 0.0f;
        eqShelfDb = 0.0f;
    }

    /** True while amount is non-zero (used to keep F0 tracking awake). */
    bool isActive() const noexcept
    {
        return amountSmooth.getCurrentValue() > 1.0e-6f;
    }

    /**
        Advances the shared control signal one sample and returns the AM gain.
        f0/clarity/voiced come from the global F0Tracker (block-rate values).
        Call exactly once per sample, before process() for L and R.
    */
    float nextGain (float f0, float clarity, bool voiced) noexcept
    {
        const float amount = amountSmooth.getNext();
        rateSmooth.getNext();
        toneSmooth.getNext();

        if (std::isfinite (f0) && f0 >= 100.0f && f0 <= 1000.0f)
            lastF0 = f0;

        const bool gateTarget = voiced
                             && std::isfinite (clarity)
                             && clarity >= 0.50f
                             && lastF0 >= 100.0f;
        const float target = gateTarget ? 1.0f : 0.0f;
        const float coeff = target > gate ? gateAttackCoeff : gateReleaseCoeff;
        gate += coeff * (target - gate);

        const float amountNorm = amount * 0.01f;

        // Audit P2-5: with no amount and a closed gate the AM oscillator,
        // the noise layer and the trigonometric terms are inaudible. Keep the
        // smoothers and the gate running (so a later Amount rise attacks
        // normally) but skip the expensive control path.
        if (amountNorm <= 1.0e-6f && gate <= 1.0e-6f)
        {
            lastGain = 1.0f;
            lastEffective = 0.0f;
            return 1.0f;
        }

        const float rate = rateSmooth.getCurrentValue();
        float fm = rate * lastF0;
        // RandomModSource continuous is ~0.58 RMS; N3 used unit-RMS noise.
        fm *= 1.0f + fmJitter * (jitterSource.next() * 1.7f);
        fm = juce::jlimit (5.0f, 400.0f, fm);
        lastAmRate = fm;

        phase += juce::MathConstants<float>::twoPi * fm / static_cast<float> (sampleRate);
        if (phase >= juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        const float cosPhi = trig::cos (phase);
        const float q = 0.5f + 0.5f * cosPhi;
        const float cos2 = 2.0f * cosPhi * cosPhi - 1.0f;

        float m = baseDepth * amountNorm
                * (1.0f + depthVariation * (depthSource.next() * 1.7f));
        m = juce::jlimit (0.0f, 0.60f, m);

        const float main = 1.0f - m * (q + secondOrder * cos2);
        const float noise = boundedNoise();
        const float raw = main - noiseDepth * amountNorm * noise;
        const float g = 1.0f + gate * (raw - 1.0f);

        lastGain = g;
        lastEffective = amountNorm * gate;
        return g;
    }

    /** Applies the shared gain and this channel's tone EQ. */
    float process (float x, int channel, float gain) noexcept
    {
        if (lastEffective <= 1.0e-6f)
            return x;

        const int ch = juce::jlimit (0, 1, channel);
        const float y = shelfEq[ch].process (peakEq[ch].process (x * gain));
        return y;
    }

    //==========================================================================
    // Diagnostics (tests)
    float getSmoothedAmount() const noexcept { return amountSmooth.getCurrentValue() * 0.01f; }
    float getLastGain() const noexcept { return lastGain; }
    float getGate() const noexcept { return gate; }
    float getEffectiveAmount() const noexcept { return lastEffective; }
    float getLastF0() const noexcept { return lastF0; }
    float getAmRate() const noexcept { return lastAmRate; }
    float getEqPeakDb() const noexcept { return eqPeakDb; }
    float getEqShelfDb() const noexcept { return eqShelfDb; }

private:
    //==========================================================================
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void reset() noexcept { z1 = z2 = 0.0; }

        void setIdentity() noexcept { b0 = 1.0; b1 = b2 = a1 = a2 = 0.0; }

        float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return static_cast<float> (y);
        }
    };

    //==========================================================================
    void updateEqCoefficients() noexcept
    {
        const float amount = amountSmooth.getCurrentValue() * 0.01f;
        const float effective = amount * gate;
        const float tone = toneSmooth.getCurrentValue();

        if (effective <= 1.0e-6f)
        {
            eqPeakDb = 0.0f;
            eqShelfDb = 0.0f;
            for (auto& f : peakEq)  { setIdentity (f); f.reset(); }
            for (auto& f : shelfEq) { setIdentity (f); f.reset(); }
            return;
        }

        const float darkness = 0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, tone / 100.0f);
        eqPeakDb = -6.0f * darkness * effective;
        eqShelfDb = -2.0f * darkness * effective;

        for (auto& f : peakEq)
            setPeak (f, 4000.0, 1.2, eqPeakDb, sampleRate);
        for (auto& f : shelfEq)
            setHighShelf (f, 8000.0, eqShelfDb, sampleRate);
    }

    float boundedNoise() noexcept
    {
        const float raw = noiseSource.next();
        noiseLp += noiseLpAlpha * (raw - noiseLp);
        const float hp = raw - noiseLp;
        return trig::tanh (hp * 4.0f);
    }

    static void setIdentity (Biquad& f) noexcept { f.setIdentity(); }

    void setPeak (Biquad& f, double fc, double q, double gainDb, double sr) noexcept
    {
        if (std::abs (gainDb) < 1.0e-6)
        {
            f.setIdentity();
            return;
        }

        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * fc / sr;
        const double c = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha / A;

        f.b0 = (1.0 + alpha * A) / a0;
        f.b1 = (-2.0 * c) / a0;
        f.b2 = (1.0 - alpha * A) / a0;
        f.a1 = (-2.0 * c) / a0;
        f.a2 = (1.0 - alpha / A) / a0;
    }

    void setHighShelf (Biquad& f, double fc, double gainDb, double sr) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        if (std::abs (gainDb) < 1.0e-6)
        {
            f.setIdentity();
            return;
        }

        const double w0 = juce::MathConstants<double>::twoPi * fc / sr;
        const double c = std::cos (w0);
        const double s = std::sin (w0);
        const double alpha = s * 0.5 * std::sqrt (2.0);
        const double sqrtA = std::sqrt (A);

        const double b0 = A * ((A + 1.0) + (A - 1.0) * c + 2.0 * sqrtA * alpha);
        const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
        const double b2 = A * ((A + 1.0) + (A - 1.0) * c - 2.0 * sqrtA * alpha);
        const double a0 = (A + 1.0) - (A - 1.0) * c + 2.0 * sqrtA * alpha;
        const double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * c);
        const double a2 = (A + 1.0) - (A - 1.0) * c - 2.0 * sqrtA * alpha;

        f.b0 = b0 / a0;
        f.b1 = b1 / a0;
        f.b2 = b2 / a0;
        f.a1 = a1 / a0;
        f.a2 = a2 / a0;
    }

    //==========================================================================
    double sampleRate = 48000.0;
    util::SmoothedValue amountSmooth, rateSmooth, toneSmooth;

    RandomModSource jitterSource, depthSource, noiseSource;
    float noiseLp = 0.0f;
    float noiseLpAlpha = 0.0f;

    float gate = 0.0f;
    float gateAttackCoeff = 0.0f;
    float gateReleaseCoeff = 0.0f;

    float phase = 0.0f;
    float lastF0 = 110.0f;
    float lastAmRate = 0.0f;
    float lastGain = 1.0f;
    float lastEffective = 0.0f;

    Biquad peakEq[2], shelfEq[2];
    float eqPeakDb = 0.0f;
    float eqShelfDb = 0.0f;
};

} // namespace dsp
