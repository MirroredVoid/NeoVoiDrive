#pragma once

/*
    FogCore — the ONE harmonic-fog implementation shared by both Rough scopes
    (T51 shared kernel; T58/T59/T60 constant-envelope phase fog. SPEC §4.3 /
    SIDEBAND_SPEC §10 / DECISIONS R45/R50).

    The two Rough knobs differ ONLY in scope:
      - Band Effects Rough applies the fog to the whole band output
        (`BandFogUnit` after the Legacy/Sideband engine mix);
      - Sideband Rough applies the fog to the sideband wet path only
        (`SidebandEngine`; the engine dry stays at full level).

    Topology (T58/T59/T60; replaces the T53–T55 delay/noise fog and AM):

        (i, q) = Hilbert(x)                         // 90 deg quadrature pair
        y      = i*cos(phi) - q*sin(phi)            // unitary phase rotation

    The quadrature pair comes from the well-known 8-section IIR all-pass
    Hilbert transformer (coefficients optimised for 20 Hz–20 kHz at 44.1 kHz;
    at 48 kHz the measured phase error stays below 2 deg over 20 Hz–20 kHz).

    `phi` is a band-limited random CONTROL signal, never added to the audio:
      - 20–200 Hz main phase noise (weight 0.70), and
      - 500–3000 Hz fine phase noise (weight 0.30),
    each normalised to unit RMS and scaled by 4*amount^2 radians (R50 = 1 rad,
    R100 = 4 rad). The mixed signal is normalised by
    1/sqrt(0.70^2 + 0.30^2). The 0.70/0.30 split is the T60 compromise: the
    old 0.75 main share left ~3.3 % of the phase-control rate power below
    300 Hz (coarse grains); T59's 0.60/0.40 cut that to ~1.0 % but raised the
    far 400–4000 Hz skirt from 0.6 % to 1.8 % (a layer under the harmonics).
    0.70/0.30 keeps ~2.2 % below 300 Hz and ~0.9 % far skirt, with a phase
    rate ~588 Hz — finer than T58, without T59's fog layer.
    T60 production figures at 1 kHz: R30 line ±5 86.11 %, inside ±100 95.40 %;
    R50 line 32.02 %, inside ±100 69.84 %. R100 is the full-fog end point.
    The energy is a broad peak around each harmonic, not a thin line plus a
    separate noise floor.

    Because the rotation is unitary on the analytic signal, it does NOT add
    amplitude modulation. This is the key T58 fix: T54/T55's low-rate AM and
    T56's symmetric frequency-shift ensemble both produced audible 10–20 Hz
    level beating; here the 5–20 Hz short-term-RMS share stays at the dry
    level (~17–18 % on the reference voice, no added pumping).

    The control RNGs only drive phase; the audio path contains no additive
    noise source and no independent fog layer. Every output component is the
    input's own harmonic content, phase-fogged.

    Rough = 0 -> the caller bypasses this core and returns x bit-exactly.
    There is no delay line and no parameter smoothing inside the core; each
    scope keeps its own amount smoothing and (Sideband) its own fixed-PDC
    delay line. Sideband-only mechanisms (periodic PM, pn phase haze, voiced
    gating, wetGain, lowcut, Bright) stay in SidebandEngine.

    RT contract: zero allocation, no locks, no exceptions, no IO.
*/

#include <juce_core/juce_core.h>

#include "TrigApprox.h"

#include <cmath>
#include <cstdint>

namespace dsp
{

class FogCore
{
public:
    /** PDC reserve only (T45/T58): Sideband's reported Lmax still includes
        the historical F budget so latency/report values stay unchanged.
        T58/T59/T60 no longer use a fog delay read. */
    static constexpr int fogMaxSamplesAt48k = 34;

    static constexpr float phaseMainLo = 20.0f;
    static constexpr float phaseMainHi = 200.0f;
    static constexpr float phaseFineLo = 500.0f;
    static constexpr float phaseFineHi = 3000.0f;
    static constexpr float phaseMainWeight = 0.70f;
    static constexpr float phaseFineWeight = 0.30f;
    /** 1 / sqrt(0.70^2 + 0.30^2): restores unit RMS after the weighted mix. */
    static constexpr float phaseMixComp = 1.3130643f;
    /** Phase deviation at Rough 100 (rad RMS). R50 uses amount^2 = 1 rad. */
    static constexpr float phaseRmsMax = 4.0f;

    /** 44.1 kHz, 20 Hz–20 kHz optimised IIR Hilbert pair. At 48 kHz the
        quadrature error is < 2 deg. */
    static constexpr float hilbertCoeffs[8] =
    {
        0.4794008656f, 0.8762184935f, 0.9765975895f, 0.9974992559f,
        0.1617584983f, 0.7330289323f, 0.9453497003f, 0.9905991567f
    };

    static float fogSamplesForRate (double sr) noexcept
    {
        return static_cast<float> (juce::jmax (1, static_cast<int> (
            std::lround (fogMaxSamplesAt48k * sr / 48000.0))));
    }

    static float hpCoeffForRate (double sr) noexcept
    {
        const auto c = 1.0f - juce::MathConstants<float>::twoPi * 300.0f
                              / static_cast<float> (sr);
        return juce::jlimit (0.0f, 0.9995f, c);
    }

    //==========================================================================
    /** 8-section all-pass Hilbert pair: i = out0, q = out1 delayed by one
        sample (the alignment used by the approved fineC listening prototype). */
    class Hilbert
    {
    public:
        void prepare (double) noexcept { reset(); }

        void reset() noexcept
        {
            stage0.reset(); stage1.reset(); stage2.reset(); stage3.reset();
            stage4.reset(); stage5.reset(); stage6.reset(); stage7.reset();
            previousInput = previousQ = 0.0f;
        }

        void process (float x, float& i, float& q) noexcept
        {
            const float xd = previousInput;
            previousInput = x;

            float y = stage0.process (xd);
            y = stage1.process (y);
            y = stage2.process (y);
            y = stage3.process (y);

            float z = stage4.process (x);
            z = stage5.process (z);
            z = stage6.process (z);
            z = stage7.process (z);

            i = y;
            // One-sample alignment used by the approved fineC prototype.
            q = previousQ;
            previousQ = z;
        }

    private:
        struct AllPass2
        {
            explicit AllPass2 (float coefficient) noexcept : a (coefficient) {}

            float process (float x) noexcept
            {
                const float y = a * (x + y2) - x2;
                x2 = x1; x1 = x;
                y2 = y1; y1 = y;
                return y;
            }

            void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

            float a;
            float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        };

        AllPass2 stage0 { hilbertCoeffs[0] };
        AllPass2 stage1 { hilbertCoeffs[1] };
        AllPass2 stage2 { hilbertCoeffs[2] };
        AllPass2 stage3 { hilbertCoeffs[3] };
        AllPass2 stage4 { hilbertCoeffs[4] };
        AllPass2 stage5 { hilbertCoeffs[5] };
        AllPass2 stage6 { hilbertCoeffs[6] };
        AllPass2 stage7 { hilbertCoeffs[7] };
        float previousInput = 0.0f, previousQ = 0.0f;
    };

    //==========================================================================
    /** Constant-envelope phase fog. The RNGs drive phase only; no audio is
        added. `amount` is the smoothed 0..1 Rough value. */
    class PhaseFog
    {
    public:
        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            mainHpCoeff = onePoleCoeff (phaseMainLo);
            mainLpCoeff = onePoleCoeff (phaseMainHi);
            fineHpCoeff = onePoleCoeff (phaseFineLo);
            fineLpCoeff = onePoleCoeff (phaseFineHi);
            calibrate();
            reset();
            mainRandom.setSeed (0x5F0C0101LL);
            fineRandom.setSeed (0x5F0C0202LL);
        }

        void setSeed (std::int64_t seed) noexcept
        {
            mainRandom.setSeed (seed ^ 0x0F0D0F0D0F0D0F0DLL);
            fineRandom.setSeed (seed ^ 0x0F0E0F0E0F0E0F0ELL);
            reset();
        }

        void reset() noexcept
        {
            hilbert.reset();
            mainHp1 = mainHp2 = 0.0f;
            mainL1 = mainL2 = mainL3 = mainL4 = 0.0f;
            fineHp1 = fineHp2 = 0.0f;
            fineL1 = fineL2 = fineL3 = fineL4 = 0.0f;
            lastPhase = 0.0f;
        }

        float process (float x, float amount) noexcept
        {
            if (amount <= 1.0e-6f)
                return x;

            float i, q;
            hilbert.process (x, i, q);

            const float mainWhite = mainRandom.nextFloat() * 2.0f - 1.0f;
            const float fineWhite = fineRandom.nextFloat() * 2.0f - 1.0f;
            const float main = mainFilter (mainWhite) * mainGain;
            const float fine = fineFilter (fineWhite) * fineGain;
            const float control = (phaseMainWeight * main
                                   + phaseFineWeight * fine) * phaseMixComp;
            // amount^2 perceptual curve: R50 = 1 rad (moderate widening),
            // R100 = 4 rad (full fog). A linear amount made R50 all-fog.
            const float a = juce::jlimit (0.0f, 1.0f, amount);
            const float depth = a * a;
            const float phi = phaseRmsMax * depth * control;
            lastPhase = phi;

            return i * trig::cos (phi) - q * trig::sin (phi);
        }

        float getLastPhase() const noexcept { return lastPhase; }

    private:
        float onePoleCoeff (float hz) const noexcept
        {
            return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz
                                    / static_cast<float> (sampleRate));
        }

        float mainFilter (float white) noexcept
        {
            mainHp1 += mainHpCoeff * (white - mainHp1);
            mainHp2 += mainHpCoeff * (mainHp1 - mainHp2);
            float y = white - mainHp2;

            mainL1 += mainLpCoeff * (y - mainL1); y = mainL1;
            mainL2 += mainLpCoeff * (y - mainL2); y = mainL2;
            mainL3 += mainLpCoeff * (y - mainL3); y = mainL3;
            mainL4 += mainLpCoeff * (y - mainL4); y = mainL4;
            return y;
        }

        float fineFilter (float white) noexcept
        {
            fineHp1 += fineHpCoeff * (white - fineHp1);
            fineHp2 += fineHpCoeff * (fineHp1 - fineHp2);
            float y = white - fineHp2;

            fineL1 += fineLpCoeff * (y - fineL1); y = fineL1;
            fineL2 += fineLpCoeff * (y - fineL2); y = fineL2;
            fineL3 += fineLpCoeff * (y - fineL3); y = fineL3;
            fineL4 += fineLpCoeff * (y - fineL4); y = fineL4;
            return y;
        }

        /** Calibrates each band to unit RMS from white noise. prepare-time
            only; resets all audio state afterwards. */
        void calibrate() noexcept
        {
            juce::Random rng (0x5F0C0A0ALL);
            reset();

            constexpr int n = 32768;
            double mainPower = 0.0, finePower = 0.0;
            for (int k = 0; k < n; ++k)
            {
                const float main = mainFilter (rng.nextFloat() * 2.0f - 1.0f);
                const float fine = fineFilter (rng.nextFloat() * 2.0f - 1.0f);
                mainPower += static_cast<double> (main) * main;
                finePower += static_cast<double> (fine) * fine;
            }

            mainGain = static_cast<float> (1.0 / std::sqrt (
                juce::jmax (1.0e-12, mainPower / static_cast<double> (n))));
            fineGain = static_cast<float> (1.0 / std::sqrt (
                juce::jmax (1.0e-12, finePower / static_cast<double> (n))));
            reset();

            if (! std::isfinite (mainGain) || ! std::isfinite (fineGain))
                mainGain = fineGain = 1.0f;
        }

        Hilbert hilbert;
        juce::Random mainRandom, fineRandom;

        double sampleRate = 48000.0;
        float mainHpCoeff = 0.0f, mainLpCoeff = 0.0f;
        float fineHpCoeff = 0.0f, fineLpCoeff = 0.0f;
        float mainHp1 = 0.0f, mainHp2 = 0.0f;
        float mainL1 = 0.0f, mainL2 = 0.0f, mainL3 = 0.0f, mainL4 = 0.0f;
        float fineHp1 = 0.0f, fineHp2 = 0.0f;
        float fineL1 = 0.0f, fineL2 = 0.0f, fineL3 = 0.0f, fineL4 = 0.0f;
        float mainGain = 1.0f, fineGain = 1.0f;
        float lastPhase = 0.0f;
    };
};

} // namespace dsp
