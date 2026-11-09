#pragma once

/*
    SidebandEngine — f0-tracking sideband roughness generator (T28,
    SIDEBAND_SPEC §3/§8/§9/§10).

    Replaces the HarmonicExciter role in the band chain (BandProcessor
    integration is T29; this unit is standalone with a f0 stub fed through
    Params).

    Topology (SIDEBAND_SPEC §3):
        in ──┬──► dry delay line (baseline L samples, R15 match) ────┐
             │                                                       ├─► out
             └──► HP(lowcut, 24 dB/oct) ──► wet delay line (PM) ──► wetGain ──► PhaseFog ─┘

    PM math: wet = x_HP(t − D(t)), D(t) = L + A·mod(t); for a component at
    frequency f the modulation index is β(f) = 2πf·A, giving f ± fm sidebands
    with amplitude ratio ≈ J1(β)/J0(β). A = βref/(2π·1000) with
    βref = 2·(depth/100) ∈ [0, 2] (1 kHz reference calibration, §3).

    mod synthesis (§3/§10, T58/T59/T60 constant-envelope phase fog):
        yWet = read(L + A·sin(θ + pn))          // sideband delay only
        wet  = PhaseFog(yWet, rough)            // unitary phase fog
        θ   += 2π·fm/sr;   fm = ratio·f0
      - A = βref/(2π·1000) with βref = 2·(depth/100) → the sideband index.
      - F = FogCore::fogSamplesForRate(sr) (34 @48k) is retained ONLY in the
        fixed Lmax = A(100 %) + F + 4 baseline for T45 PDC compatibility.
        T58 no longer reads a fog delay; the phase fog is zero-latency.
      - T58: the old noise-modulated delay fog and the AM branch are gone.
        The wet tap is the PM read only; FogCore then applies a constant-
        envelope phase fog to the wet signal. The phase rotation is unitary,
        so it adds no amplitude modulation and no additive noise layer; all
        output energy is the wet signal's own harmonics. fineC/T59/T60's phase
        control (20–200 Hz at 0.70 + 500–3000 Hz at 0.30, 4 rad RMS at R100)
        suppresses the central line and leaves a broad peak at every harmonic. The sideband
        scope still sums the engine dry at full level (T51 scope rule); Band
        Effects Rough applies the same core to the whole band instead.
      - Rough 100 is the fully fog-covered end point; the "widened but still
        present" character is evaluated at moderate Rough (≈30–50).
      - pn  : phase haze inside the modulator phase, ±0.45·rough rad,
              700…1400 Hz — wanders the sideband lines instead of moving the
              whole copy. This is sideband-carrier-specific, not part of the
              shared fog core.
      - rough = 0 -> no phase fog; one read only, bit-identical to the clean
              sideband wet path (the Lmax F reserve remains for PDC).

    State machine (SIDEBAND_SPEC §7, engine side): wetGain ramps in over
    50 ms when voiced (Voiced or Hold), fades out over 100 ms when unvoiced.
    Wet path runs only when enable && depth > 0 && voiced; otherwise the wet
    delay line is drained with zeros (alignment kept) and the modulators are
    frozen (phase-continuous resume, no click).

    Latency (§9.1): T45 uses a FIXED baseline Lmax = A(100 %) + F + 4 samples
    (48 kHz: 53) for every depth; the dry is matched to Lmax and the value is
    reported as Lmax / 2048+Lmax while the Sideband engine is selected — see
    SPEC §5.1 and PluginProcessor.
    Diagnostics expose the current L via getLatencyCompensationSamples().

    T40 Bright (SIDEBAND_SPEC §10.1): an INDEPENDENT, purely linear brightness
    axis, orthogonal to roughness. Applied to the BAND OUTPUT (dry + wet; the
    wet alone in the test-only solo), gated in over 50 ms while the module is in
    circuit (enable && depth > 0 — no voiced check, so unvoiced syllables do
    not flutter the tone):

        out += bright * gate * (tiltGain * HP1.5k(out) + bellGain * BP3.2k,Q2(out))

    - HP1.5k is a first-order high-pass (x - onePoleLowpass(x), 6 dB/oct lift,
      ~+8 dB at 8–10 kHz for bright = 100 %), the bell is an RBJ constant
      0 dB-peak band-pass at 3.2 kHz (its centre is in phase with x, so the sum
      is an additive presence boost, never a two-filter phase cancellation);
    - it is NOT wet-only on purpose: the untouched dry masks a wet-only lift
      (measured +0.6 dB on a saw / +2.4 dB on a 3.2 kHz tone), which is exactly
      the "still dull" report this feature answers;
    - it does NOT touch the pn/fn noise sources, fm, the delay or the latency;
    - bright == 0 -> the whole stage is skipped (states cleared) and the output
      is BIT-IDENTICAL to the pre-T40 engine with zero extra CPU;
    - both filters are min-phase and stable; coefficients are fixed per sample
      rate and computed in prepare() (no per-sample coefficient churn).

    RT contract: process/setParameters are zero allocation, no locks, no
    exceptions. All state is fixed-size; ModDelayLine buffers are compile-time
    arrays (8192 ≥ 66 needed at 192 kHz, SIDEBAND_SPEC §11).
*/

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

#include "FogCore.h"
#include "ModDelayLine.h"
#include "RandomModSource.h"
#include "TrigApprox.h"
#include "../util/SmoothedValue.h"

namespace dsp
{

class SidebandEngine
{
public:
    /** Per-block parameter bundle (fed from ParamSnapshot + F0Tracker). */
    struct Params
    {
        float f0        = 0.0f;    // Hz (F0Tracker output; 0 while unvoiced)
        float clarity   = 0.0f;    // 0..1 (diagnostic passthrough)
        bool  voiced    = false;   // F0Tracker::isVoiced() (Voiced || Hold)
        float depth     = 0.0f;    // %  (0 = fully transparent, §4.3)
        float fmRatio   = 0.333f;  // fm / f0, [0, 0.5] (SPEC §4.3 default 1/3)
        float roughness = 20.0f;   // %
        float lowcut    = 2000.0f; // Hz, [100, 8000] (SPEC §4.3 default 2000)
        float bright    = 0.0f;    // % T40 brightness axis [0, 100] (default 0)
        bool  enable    = true;    // hard switch (C4)
    };

    /** Smoothing columns (SIDEBAND_SPEC §4.2). */
    static constexpr float depthSmoothingMs     = 10.0f;
    static constexpr float ratioSmoothingMs     = 20.0f;
    static constexpr float roughnessSmoothingMs = 20.0f;
    static constexpr float lowcutSmoothingMs    = 20.0f;
    static constexpr float wetGainAttackMs      = 50.0f;   // §7 unvoiced→voiced
    static constexpr float wetGainReleaseMs     = 100.0f;  // §7 hold→unvoiced
    static constexpr float lowcutXfadeMs        = 20.0f;   // §8 coefficient crossfade
    static constexpr float brightSmoothingMs    = 20.0f;   // T40 (SPEC §4.3)
    static constexpr float brightGateMs         = 50.0f;   // T40 in-circuit fade

    // T40 brightness stage (SIDEBAND_SPEC §10.1): fixed linear filters.
    static constexpr float brightTiltFc         = 1500.0f; // Hz, one-pole HP
    static constexpr float brightTiltGain       = 1.8f;    // max additive HP gain
    static constexpr float brightBellFc         = 3200.0f; // Hz, presence bell
    static constexpr float brightBellQ          = 2.0f;
    static constexpr float brightBellGain       = 1.2f;    // max additive BP gain

    /** T43: smallest delay the 4-tap interpolator accepts (< 1 would wrap). */
    static constexpr float minUsableDelay       = 1.5f;

    //==========================================================================
    //  T43 "fine fog" (SIDEBAND_SPEC §10) — implementation lives in FogCore
    //  (T51). Only the sideband-only pn bandwidth stays here.
    //==========================================================================
    /** pn phase-haze bandwidth, mapped by roughness in setParameters(). */
    static constexpr float pnRateLo             = 700.0f;
    static constexpr float pnRateHi             = 1400.0f;

    /** T45: fixed worst-case sideband swing (depth = 100 %). The baseline L
        uses this value for EVERY depth so the reported latency, the global
        dry/Bypass path and all bands stay time-aligned; depth only scales the
        modulation inside that fixed window. */
    static float maxSwingSamplesForRate (double sr) noexcept
    {
        return static_cast<float> (
            (2.0 / (juce::MathConstants<double>::twoPi * 1000.0)) * sr);
    }

    /** Fixed sideband baseline L = A(100 %) + F + 4, in samples (T45). */
    static float maxDelaySamplesForRate (double sr) noexcept
    {
        return maxSwingSamplesForRate (sr) + FogCore::fogSamplesForRate (sr) + 4.0f;
    }

    /** Fixed sideband baseline L, rounded for the host report / integer taps. */
    static int maxLatencySamplesForRate (double sr) noexcept
    {
        return static_cast<int> (std::lround (maxDelaySamplesForRate (sr)));
    }

    void prepare (double newSampleRate, int /*maxBlockSize*/) noexcept
    {
        sampleRate = newSampleRate;

        depthSmooth.prepare     (sampleRate, depthSmoothingMs);
        ratioSmooth.prepare     (sampleRate, ratioSmoothingMs);
        roughnessSmooth.prepare (sampleRate, roughnessSmoothingMs);
        lowcutSmooth.prepare    (sampleRate, lowcutSmoothingMs);
        brightSmooth.prepare    (sampleRate, brightSmoothingMs);
        brightGate.prepare      (sampleRate, brightGateMs);

        depthSmooth.reset (0.0f);
        ratioSmooth.reset (0.333f);
        roughnessSmooth.reset (20.0f);
        lowcutSmooth.reset (2000.0f);
        brightSmooth.reset (0.0f);
        brightGate.reset (0.0f);

        dryLine = std::make_unique<ModDelayLine>();
        wetLine = std::make_unique<ModDelayLine>();
        dryLine->prepare (sampleRate, 0);
        wetLine->prepare (sampleRate, 0);

        // T36 rev18: independent per-voice pn sources (sideband-only).
        // T58's shared phase fog lives in FogCore; the F samples are retained
        // only in the reported Lmax for T45 PDC compatibility.
        maxSwingSamples = maxSwingSamplesForRate (sampleRate);
        pnHpCoeff = FogCore::hpCoeffForRate (sampleRate);

        for (auto& s : pnSrc) s.prepare (sampleRate);
        phaseFog.prepare (sampleRate);

        // Reset the wet-gain one-pole to zero (startup = unvoiced, §7).
        wetGainCurrent = 0.0f;
        wetGainTarget  = 0.0f;
        attackAlphaCached = 1.0f - std::exp (-1.0f / (static_cast<float> (sampleRate) * 0.050f));
        releaseAlphaCached = 1.0f - std::exp (-1.0f / (static_cast<float> (sampleRate) * 0.100f));
        wetGainAlpha   = releaseAlphaCached;

        reset();
        recomputeLowcut (2000.0f);   // initial coefficient pair
        recomputeBrightFilters();    // T40 (fixed coefficients for this rate)
        for (auto pair : { std::pair { &hpOld1, &hpNew1 }, std::pair { &hpOld2, &hpNew2 },
                           std::pair { &hpPostOld1, &hpPostNew1 }, std::pair { &hpPostOld2, &hpPostNew2 } })
        {
            auto* o = pair.first; auto* n = pair.second;
            o->b0 = n->b0; o->b1 = n->b1; o->b2 = n->b2;
            o->a1 = n->a1; o->a2 = n->a2;
            o->z1 = n->z1; o->z2 = n->z2;
        }
    }

    /** L/R share the same seed; bands derive different streams (R6 revision).
        T36 rev18: every voice gets its own RNG stream. */
    void setSeed (std::int64_t seed) noexcept
    {
        for (int k = 0; k < 3; ++k)
            pnSrc[static_cast<size_t> (k)].setSeed (seed ^ (0x5EED0100LL + k));

        // T58: the phase-control RNGs are seeded deterministically. L/R share
        // one seed -> mono stays mono (SB-U18).
        phaseFog.setSeed (seed ^ 0x5EED0200LL);
    }

    /** Clears state (delays, filters, phases). Seeds are kept. */
    void reset() noexcept
    {
        if (dryLine != nullptr) dryLine->reset();
        if (wetLine != nullptr) wetLine->reset();
        hpOld1.reset();
        hpOld2.reset();
        hpNew1.reset();
        hpNew2.reset();
        hpPostOld1.reset();
        hpPostOld2.reset();
        hpPostNew1.reset();
        hpPostNew2.reset();
        lowcutXfade.reset (1.0f);   // new coefficients already active
        for (auto& p : modPhaseK) p = 0.0f;
        for (auto& p : pnHp)    p = 0.0f;
        for (auto& p : pnPrev)  p = 0.0f;
        phaseFog.reset();
        phaseNoise = 0.0f;
        delayClampCount = 0;   // T43 diagnostic
        // T45: use the rounded report value as the actual baseline too, so
        // depth-0 transparency is an exact integer-sample delay.
        currentL = static_cast<float> (maxLatencySamplesForRate (sampleRate));
        currentA = 0.0f;
        fm = 0.0f;
        prevLowcutTarget = -1.0f;
        // T40: brightness stage states (the smoothed control itself is reset
        // in prepare(), like every other control column).
        brightLpState = 0.0f;
        bellZ1 = 0.0;
        bellZ2 = 0.0;
        brightGate.reset (0.0f);   // T40: out of circuit after a reset
        // O5 (review): reset is self-contained — the wet-gain state (incl.
        // the release alpha for a cold start, §7 startup = unvoiced).
        wetGainCurrent = 0.0f;
        wetGainTarget  = 0.0f;
        wetGainAlpha   = releaseAlphaCached;
    }

    /** Once per block: updates smoothed control targets (audio thread). */
    void setParameters (const Params& p) noexcept
    {
        // O2 (review): NaN-proof every parameter (juce::jlimit passes NaN
        // through); non-finite values fall back to the SPEC defaults.
        const auto sane = [] (float v, float fallback) noexcept
        {
            return std::isfinite (v) ? v : fallback;
        };
        const float depth     = juce::jlimit (0.0f, 100.0f, sane (p.depth, 0.0f));
        const float ratio     = juce::jlimit (0.0f, 0.5f,  sane (p.fmRatio, 0.333f));
        const float roughness = juce::jlimit (0.0f, 100.0f, sane (p.roughness, 20.0f));
        const float lowcut    = juce::jlimit (100.0f, 8000.0f, sane (p.lowcut, 2000.0f));
        const float bright    = juce::jlimit (0.0f, 100.0f, sane (p.bright, 0.0f));  // T40
        const float f0        = (p.f0 > 0.0f && std::isfinite (p.f0)) ? p.f0 : 0.0f;

        // §4.3: enable=off is equivalent to depth forced to 0 — the wet path
        // AND the baseline L stay pinned at their depth-0 values, so flipping
        // any other sideband parameter while disabled changes nothing.
        const float effectiveDepth = p.enable ? depth : 0.0f;

        f0Target      = f0;
        clarityTarget = p.clarity;
        voicedTarget  = p.voiced;
        enableTarget  = p.enable;
        wetActiveTarget = enableTarget && effectiveDepth > 0.0f && voicedTarget;

        depthSmooth.setTarget (effectiveDepth);
        ratioSmooth.setTarget (ratio);
        roughnessSmooth.setTarget (roughness);
        lowcutSmooth.setTarget (lowcut);
        brightSmooth.setTarget (bright);   // T40 (orthogonal to roughness)
        // T40: the tone stage is "in circuit" while the sideband module is
        // enabled and depth > 0 — deliberately WITHOUT the voiced check, so
        // unvoiced syllables keep a constant tone instead of fluttering.
        brightGate.setTarget ((enableTarget && effectiveDepth > 0.0f) ? 1.0f : 0.0f);

        // Fixed-rate pn bandwidth (control domain, once per block). T58's
        // phase fog has fixed 20–200 / 500–3000 Hz control bands inside
        // FogCore; only the sideband-only pn rate still follows rough.
        const float rough01 = roughness / 100.0f;
        const float pnRate  = pnRateLo + (pnRateHi - pnRateLo) * rough01;
        for (auto& s : pnSrc) s.setRate (pnRate);   // pn is sideband-only

        // Lowcut coefficient recalc + 20 ms crossfade when the target moves.
        // S1/M2 (review): adopt the CURRENTLY EFFECTIVE coefficients (hpNew,
        // coefficients + state) as the crossfade start point BEFORE computing
        // the new ones — hpOld must track the last applied lowcut, not the
        // prepare-time 500 Hz. The xfade then ramps 0→1 (setTarget(1.0f);
        // reset() alone zeroes the target and would pin the fade at hpOld).
        if (std::abs (lowcut - prevLowcutTarget) > 0.5f)
        {
            hpOld1 = hpNew1;
            hpOld2 = hpNew2;
            // T36 rev37d: the post stage mirrors the coefficient swap; its
            // filter STATE is per-instance and must NOT be copied (copying
            // hpNew's zeroed state into hpPostOld would blank its history).
            {
                auto copyCoeffs = [] (BiquadHP& dst, const BiquadHP& src)
                {
                    dst.b0 = src.b0; dst.b1 = src.b1; dst.b2 = src.b2;
                    dst.a1 = src.a1; dst.a2 = src.a2;
                };
                copyCoeffs (hpPostOld1, hpPostNew1);
                copyCoeffs (hpPostOld2, hpPostNew2);
            }
            recomputeLowcut (lowcut);
            lowcutXfade.prepare (sampleRate, lowcutXfadeMs);
            lowcutXfade.reset (0.0f);
            lowcutXfade.setTarget (1.0f);
            prevLowcutTarget = lowcut;
        }
    }

    /** One sample in, one sample out (audio thread). */
    float process (float x) noexcept
    {
        // Smoothed controls (per sample).
        const float depthS = depthSmooth.getNext();
        const float ratioS = ratioSmooth.getNext();
        const float roughS = roughnessSmooth.getNext() / 100.0f;   // 0..1
        const float brightS = brightSmooth.getNext();              // T40: 0..100 %

        // Swing amplitude (samples). The baseline is FIXED at the depth-100
        // worst case so every depth shares one reported latency and one dry
        // path (T45); depth only scales the modulation inside that window.
        const float betaRef = 2.0f * depthS / 100.0f;              // βref ∈ [0, 2]
        currentA = static_cast<float> (betaRef / (juce::MathConstants<float>::twoPi * 1000.0f)
                                       * static_cast<float> (sampleRate));

        // Wet gain: voiced → depth/100 (50 ms attack); unvoiced → 0 (100 ms
        // release). When the wet path is inactive the gain stays 0.
        if (wetActiveTarget)
        {
            wetGainTarget = depthS / 100.0f;
            wetGainAlpha = attackAlphaCached;
        }
        else
        {
            wetGainTarget = 0.0f;
            wetGainAlpha = releaseAlphaCached;
        }
        wetGainCurrent += wetGainAlpha * (wetGainTarget - wetGainCurrent);

        // Dry: delay-matched baseline (R15). MUST keep processing in solo
        // too (stream alignment for the wet delay line is shared logic).
        const float dry = dryLine->process (x, currentL);

        const bool wetActive = wetActiveTarget || wetGainCurrent > 1e-4f;
        float wet = 0.0f;

        // The lowcut crossfade advances UNCONDITIONALLY (both branches):
        // freezing it during an inactive block would pin the mix at the old
        // filter after a lowcut change — and snapTo() would also clear the
        // fade target (S1 follow-up: a change while unvoiced must still take
        // effect once the wet path re-activates).
        const float xw = lowcutXfade.getNext();

        if (wetActive)
        {
            // Lowcut: 24 dB/oct Butterworth HP (T39: two cascaded biquads),
            // double state (R7), with the 20 ms old→new coefficient crossfade
            // (§8). O1 (review): the weight is read ONCE per sample.
            const float stageOut1 = (1.0f - xw) * hpOld1.process (x) + xw * hpNew1.process (x);
            const float hpOut     = (1.0f - xw) * hpOld2.process (stageOut1) + xw * hpNew2.process (stageOut1);

            const float twoPi = juce::MathConstants<float>::twoPi;

            // fm = f0·ratio, clamped (T36 rev29: the deterministic wobble is
            // REMOVED — it swept the line ±11 Hz past any fixed observation
            // point, which reads as 25 dB/20 ms "sideband dips" at fixed
            // times (the user's pump). A stationary line = a stationary
            // fixed-point energy = no dips.
            fm = juce::jlimit (1.0f, 8000.0f, f0Target * ratioS);

            //==================================================================
            // T58/T59/T60 constant-envelope phase fog (SIDEBAND_SPEC §10)
            //==================================================================
            // The wet delay is the PM read only; no noise fog and no AM.
            // FogCore then rotates the wet signal's analytic phase. The
            // rotation is unitary: no level pumping, no separate noise layer.
            const float pnAmp = 0.45f * roughS;

            // Phase haze: pn rides INSIDE the modulator phase (sideband line
            // wander); this remains sideband-only.
            const float pnS = pnSrc[0].next();
            pnHp[0] = pnHpCoeff * (pnHp[0] + pnS - pnPrev[0]);
            pnPrev[0] = pnS;

            const float wk = twoPi * fm / static_cast<float> (sampleRate);
            modPhaseK[0] = std::fmod (modPhaseK[0] + wk, twoPi);
            const float modClean = trig::sin (modPhaseK[0]);
            const float modFog   = trig::sin (modPhaseK[0] + pnAmp * pnHp[0]);
            phaseNoise = pnAmp * pnHp[0];   // diagnostics: voice 1's pn

            const float delayCleanRaw = currentL + currentA * modClean;
            const float delayFogRaw   = currentL + currentA * modFog;

            // SB-U27 crash guard: a negative delay would spin the read
            // pointer. The fixed Lmax still includes the historical F reserve
            // (T45 PDC compatibility), even though T58 has no fog read.
            if (delayCleanRaw < minUsableDelay || delayFogRaw < minUsableDelay)
                ++delayClampCount;
            const float delayClean = juce::jmax (minUsableDelay, delayCleanRaw);
            const float delayFog   = juce::jmax (minUsableDelay, delayFogRaw);

            if (roughS > 0.0f)
            {
                const float tap = wetLine->process (hpOut, delayFog);
                wet = phaseFog.process (wetGainCurrent * tap, roughS);
            }
            else
            {
                // roughness 0: no phase fog -> clean sideband wet path.
                wet = wetGainCurrent * wetLine->process (hpOut, delayClean);
                phaseFog.reset();
            }
        }
        else
        {
            // Inactive: drain the wet line with zeros, freeze the modulators
            // (phases resume where they stopped → no click on reactivation).
            // The lowcut xfade keeps advancing (handled above) so a change
            // made while inactive is already settled on reactivation.
            wet = wetLine->process (0.0f, currentL);
            phaseFog.reset();
        }

        // T36 rev37d: POST-modulation lowcut. The pre-delay HP cannot cover
        // the sideband path: PM through the variable delay MOVES energy
        // (intermod of the signal with the modulator creates NEW low-side
        // components below the lowcut — measured as mid-low "clicks" and a
        // low-frequency shelf in the spectrogram that survived lowcut=8000).
        // The SAME Butterworth pair (per xfade old/new) runs again on the
        // wet output so EVERY sideband component obeys the knob.
        {
            const float w1 = (1.0f - xw) * hpPostOld1.process (wet) + xw * hpPostNew1.process (wet);
            wet =          (1.0f - xw) * hpPostOld2.process (w1)  + xw * hpPostNew2.process (w1);
        }

        // T40 brightness (SIDEBAND_SPEC §10.1): a purely linear, orthogonal
        // tone stage on the BAND OUTPUT (dry + wet; solo wet in test mode):
        //   out += b·(1.8·HP1.5k(out) + 1.2·BP3.2k(out))
        // It is deliberately NOT wet-only: boosting the wet alone is masked by
        // the untouched dry (measured +0.6 dB on a saw, +2.4 dB on a 3.2 kHz
        // tone) while the user wants the BAND to open up. The stage is linear,
        // additive/monophase at the bell centre (no two-filter cancellation),
        // no waveshaper, no new RNG, no delay/latency change. It fades in with
        // a 50 ms gate while the sideband module is in circuit (depth > 0 &
        // enabled) — the same condition as the wet path minus the voiced check,
        // so unvoiced syllables do NOT flutter the tone. bright == 0 (layout
        // default) skips the whole stage: bit-identical to pre-T40, zero CPU.
        const float brightAmt = brightS * 0.01f * brightGate.getNext();
        float out = wetSoloForTest ? wet : (dry + wet);

        if (brightAmt > 1e-6f)
        {
            // First-order high-pass lift (x − one-pole LP): +6 dB/oct from
            // the 1.5 kHz corner, ~+8 dB at 8–10 kHz for bright = 100 %.
            brightLpState += brightLpAlpha * (out - brightLpState);
            const float hp = out - brightLpState;

            // RBJ constant-0 dB-peak band-pass (TDF-II, double state):
            // +6.8 dB presence bell at 3.2 kHz for bright = 100 % (the
            // "metal core" emphasis; centre phase 0° → clean boost).
            const double bpOut = bellB0 * out + bellZ1;
            bellZ1 = bellB1 * out - bellA1 * bpOut + bellZ2;
            bellZ2 = bellB2 * out - bellA2 * bpOut;

            out += brightAmt * (brightTiltGain * hp
                                + brightBellGain * static_cast<float> (bpOut));
        }
        else
        {
            // Not in circuit: clear the states so a later enable cannot
            // replay stale filter history.
            brightLpState = 0.0f;
            bellZ1 = 0.0;
            bellZ2 = 0.0;
        }

        return out;
    }

    //==========================================================================
    //  Diagnostics (tests / T29)
    //==========================================================================
    /** Fixed baseline delay Lmax in samples (dry/wet matched, T45). */
    float getLatencyCompensationSamples() const noexcept { return currentL; }
    float getWetGain() const noexcept { return wetGainCurrent; }
    float getFm() const noexcept { return fm; }
    float getClarity() const noexcept { return clarityTarget; }
    bool  isWetPathActive() const noexcept { return wetActiveTarget || wetGainCurrent > 1e-4f; }
    /** Lowcut crossfade weight 0..1 (diagnostics; does not advance the ramp). */
    float getLowcutCrossfade() const noexcept { return lowcutXfade.getCurrentValue(); }
    /** Current phase-noise offset in radians (T36 rev18: voice 1's pn,
        |·| ≤ 0.45 at r=1). */
    float getPhaseNoise() const noexcept { return phaseNoise; }
    /** T40: current smoothed Bright value in % (diagnostics / tests). */
    float getBright() const noexcept { return brightSmooth.getCurrentValue(); }
    /** T43: samples whose RAW delay fell below the interpolation floor
        (`minUsableDelay`), i.e. the read was clamped. Must stay 0 (SB-U27). */
    std::int64_t getDelayClampCount() const noexcept { return delayClampCount; }
    /** T58: last phase-fog offset in radians (diagnostics). */
    float getPhaseFogRadians() const noexcept { return phaseFog.getLastPhase(); }
    /** Test-only wet solo (used by the Bright tests to isolate the wet path;
        there is no corresponding user parameter). */
    void setWetSoloForTest (bool on) noexcept { wetSoloForTest = on; }

    /** Test hook: seed the modulator phases (M1 wrap regression). */
    void setModulatorPhaseForTest (float phase1, float phase2) noexcept
    {
        modPhaseK[0] = phase1;
        modPhaseK[1] = phase2;
    }

private:
    //==========================================================================
    //  Butterworth high-pass biquad (TDF-II, double state, R7)
    //==========================================================================
    struct BiquadHP
    {
        void reset() noexcept { z1 = z2 = 0.0; }

        float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return static_cast<float> (y);
        }

        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };

        // T39: 24 dB/oct = TWO cascaded Butterworth biquads (Q = 1/√2 each).
        void recomputeLowcut (float fc) noexcept
        {
            const double w0  = juce::MathConstants<double>::twoPi * fc / sampleRate;
            const double cs  = std::cos (w0);
            const double sn  = std::sin (w0);
            const double alpha = sn / std::sqrt (2.0);   // Q = 1/√2 (Butterworth)

            const double b0 = (1.0 + cs) * 0.5;
            const double b1 = -(1.0 + cs);
            const double b2 = b0;
            const double a0 = 1.0 + alpha;
            const double a1 = -2.0 * cs;
            const double a2 = 1.0 - alpha;

            const double invA0 = 1.0 / a0;
            for (auto* hp : { &hpNew1, &hpNew2 })
            {
                hp->b0 = b0 * invA0;
                hp->b1 = b1 * invA0;
                hp->b2 = b2 * invA0;
                hp->a1 = a1 * invA0;
                hp->a2 = a2 * invA0;
                hp->z1 = hp->z2 = 0.0;
            }
            // T36 rev37d: the post-modulation stage shares these coefficients
            // (its own state lives in its instances; only copy coefficients).
            for (auto* hp : { &hpPostNew1, &hpPostNew2 })
            {
                hp->b0 = b0 * invA0;
                hp->b1 = b1 * invA0;
                hp->b2 = b2 * invA0;
                hp->a1 = a1 * invA0;
                hp->a2 = a2 * invA0;
                hp->z1 = hp->z2 = 0.0;
            }

        // hpOld keeps the PREVIOUS coefficients (crossfade pair); prepare()
        // seeds both filters identically.
    }

    // T40: fixed brightness-stage coefficients (computed once per sample rate;
    // the controls only scale the two filtered branches, so no coefficient
    // crossfades are needed — unlike the lowcut).
    void recomputeBrightFilters() noexcept
    {
        brightLpAlpha = static_cast<float> (1.0 - std::exp (
            -juce::MathConstants<double>::twoPi * brightTiltFc / sampleRate));

        const double w0    = juce::MathConstants<double>::twoPi * brightBellFc / sampleRate;
        const double cs    = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * static_cast<double> (brightBellQ));
        const double a0    = 1.0 + alpha;

        bellB0 = alpha / a0;
        bellB1 = 0.0;
        bellB2 = -alpha / a0;
        bellA1 = (-2.0 * cs) / a0;
        bellA2 = (1.0 - alpha) / a0;
        bellZ1 = 0.0;
        bellZ2 = 0.0;
    }

    //==========================================================================
    double sampleRate = 48000.0;
    float attackAlphaCached = 0.0f;
    float releaseAlphaCached = 0.0f;

    util::SmoothedValue depthSmooth, ratioSmooth, roughnessSmooth, lowcutSmooth;
    util::SmoothedValue brightSmooth;  // T40: 0..100 %
    util::SmoothedValue brightGate;    // T40: in-circuit fade (0/1, 50 ms)
    util::SmoothedValue lowcutXfade;   // 0→1 over 20 ms after a coefficient change

    // T40 brightness stage (fixed linear filters; SIDEBAND_SPEC §10.1).
    float brightLpAlpha = 0.2f;        // one-pole LP alpha (1.5 kHz @ 48 kHz)
    double bellB0 = 0.0, bellB1 = 0.0, bellB2 = 0.0;
    double bellA1 = 0.0, bellA2 = 0.0; // RBJ 0 dB-peak BPF (3.2 kHz, Q=2)
    float brightLpState = 0.0f;        // one-pole LP state
    double bellZ1 = 0.0, bellZ2 = 0.0; // TDF-II band-pass state

    // Heap-allocated: ModDelayLine embeds an 8192-sample std::array (64 KB of
    // stack per line) — with 6 band instances per MultibandEngine the inline
    // members would overflow the 1 MB default stack. prepare() allocates.
    std::unique_ptr<ModDelayLine> dryLine, wetLine;
    BiquadHP hpOld1, hpOld2, hpNew1, hpNew2;   // T39: 24 dB/oct cascade (pre-delay)
    // T36 rev37d: POST-modulation lowcut stage — same coefficients as the
    // pre-delay pair, but its OWN filter state (cascading the wet output
    // through the SAME instances would corrupt both stages' delay lines).
    BiquadHP hpPostOld1, hpPostOld2, hpPostNew1, hpPostNew2;

    // T36 rev18 MicroVoice Fog: pn stays sideband-only; T58's constant-
    // envelope phase fog is shared via FogCore.
    std::array<RandomModSource, 3> pnSrc;
    FogCore::PhaseFog phaseFog;

    // T45/T58 delay baseline
    float maxSwingSamples = 0.0f;      // T45: A at depth 100 %, fixed baseline
    float pnHpCoeff = 0.96f;           // pn DC blocker (shared 300 Hz corner)

    // Control state (block targets)
    float f0Target      = 0.0f;
    float clarityTarget = 0.0f;
    bool  voicedTarget  = false;
    bool  enableTarget  = true;
    bool  wetActiveTarget = false;
    bool  wetSoloForTest = false;    // tests only (no user parameter)
    float prevLowcutTarget = -1.0f;

    // Per-sample state
    float currentA = 0.0f;         // swing amplitude (samples)
    float currentL = 4.0f;         // baseline delay (samples)
    float fm = 0.0f;
    std::array<float, 3> modPhaseK {};   // per-voice phase accumulators
    std::array<float, 3> pnHp {};        // per-voice DC-blocked pn state
    std::array<float, 3> pnPrev {};      // per-voice pn history
    float phaseNoise = 0.0f;             // voice 1's pn (diagnostics)
    std::int64_t delayClampCount = 0;    // T43: raw delay below the usable floor
    float wetGainCurrent = 0.0f;
    float wetGainTarget  = 0.0f;
    float wetGainAlpha   = 0.0f;

    // T36 rev18: fixed voice decorrelation parameters. voicePhase is 120°
    // symmetric: the m-th sideband of voice k carries phase m·φ_k, so the
    // J1/J2 deterministic lines cancel in the sum (|1+e^{j120°}+e^{j240°}|=0
    // for m=1,2) — only the decorrelated noise haze remains in-band (J3 is
    // tripled but sits at ±3·fm, outside the sideband window). r=0 uses
    // voice 1 alone → full deterministic lines, identical to rev17.
    static constexpr std::array<float, 3> voicePhase = { 0.0f, 2.0943951f, 4.1887902f };
    static constexpr std::array<float, 3> voiceDet   = { 0.0f, -0.0001f, 0.0001f };
};

} // namespace dsp
