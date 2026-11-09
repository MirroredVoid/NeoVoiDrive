#pragma once

/*
    Per-band signal chain assembly (T11, ARCHITECTURE §2/§3; T29 dual engine;
    T49 whole-band fog):

        PitchModUnit -> ShimmerUnit -> (HarmonicExciter | SidebandEngine)
                     -> BandFogUnit -> band gain / Band On bypass

    One instance per frequency band (MultibandEngine owns 1..3).  The band's
    parameters arrive as a params::ParamSnapshot::Band POD (per-block, RT
    thread) and are fanned out to the effect units.

    T29 (REFACTOR_PLAN §2): the legacy HarmonicExciter and the new
    SidebandEngine coexist. engine_version (setEngineVersion, block boundary)
    selects the active engine through a 5 ms output crossfade. During the
    fade BOTH engines run (instant double CPU, acceptable); once settled only
    the active engine is processed (R9 — the inactive engine costs nothing).
    The SidebandEngine consumes the global F0 broadcast
    (setF0Broadcast: f0/clarity/voiced from MultibandEngine's F0Tracker).

    Transparency: each unit is internally transparent at zero parameters
    (PitchModUnit: zero depth is bit-transparent; ShimmerUnit: depth 0 ->
    gain exactly 1; HarmonicExciter: drive 0 -> y == x exactly AND its
    oversampler/waveshaper are skipped — R9). O4 (review): the "sample-for-
    sample" promise holds for the legacy chain; the sideband engine adds a
    FIXED baseline delay Lmax = A(100 %) + F + 4 that is dry-matched inside
    the unit and reported as Lmax / 2048+Lmax while active (T45,
    SIDEBAND_SPEC §9.1).

    Zero algorithmic latency otherwise: all units are zero-latency (the fixed
    sideband baseline Lmax ≤ ~201 samples at 192 kHz is reported to the host,
    SIDEBAND_SPEC §9.1). T40 Bright is a purely linear post-lowcut tone stage —
    no delay. T49 BandFog (UI name Rough, T50) is a constant-envelope Hilbert
    phase rotation (T58) with no delay line; Rough = 0 is bit-transparent.

    RT contract: zero allocation, no locks, no exceptions, no IO.
*/

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>

#include "../params/LoudnessCurve.h"
#include "../params/ParamSnapshot.h"
#include "../util/SmoothedValue.h"
#include "BandFogUnit.h"
#include "HarmonicExciter.h"
#include "PitchModUnit.h"
#include "ShimmerUnit.h"
#include "SidebandEngine.h"

namespace dsp
{

class BandProcessor
{
public:
    /** Configures sample rate and resets every unit in the chain. */
    void prepare (double newSampleRate, int maxBlockSize) noexcept
    {
        pitchMod.prepare (newSampleRate, maxBlockSize);
        shimmer.prepare (newSampleRate);
        exciter.prepare (newSampleRate, maxBlockSize);
        sideband.prepare (newSampleRate, maxBlockSize);
        bandFog.prepare (newSampleRate);   // T49

        // T45: the Band On bypass path must stay on the same fixed sideband
        // baseline as the processed path so toggling Band On never changes
        // the reported latency or the time alignment of the band sum.
        bypassDelay = std::make_unique<ModDelayLine>();
        bypassDelay->prepare (newSampleRate, 0);
        bypassDelaySamples = static_cast<float> (
            SidebandEngine::maxLatencySamplesForRate (newSampleRate));

        // Band output gain (SPEC §4.3): 20 ms smoothing, start at 0 dB.
        bandGainSmooth.prepare (newSampleRate, 20.0);
        bandGainSmooth.reset (0.0f);
        // T38 band master switch: 5 ms bypass crossfade, start enabled.
        bandEnableSmooth.prepare (newSampleRate, 5.0);
        bandEnableSmooth.reset (1.0f);

        // T29: engine crossfade (5 ms, REFACTOR_PLAN §3.2); 0 = Legacy, 1 = Sideband.
        engineXfade.prepare (newSampleRate, engineCrossfadeMs);
        engineXfade.reset (0.0f);
        engineTarget = false;
        engineTargetInitialised = false;
        parametersInitialised = false;
    }

    /** T29: selects the engine (block boundary). The switch glides through a
        5 ms equal-power crossfade. T45: the first call snaps to the startup
        engine (no startup fade / latency mismatch); later calls crossfade. */
    void setEngineVersion (bool useSideband) noexcept
    {
        if (! engineTargetInitialised)
        {
            engineTarget = useSideband;
            engineXfade.reset (useSideband ? 1.0f : 0.0f);
            engineTargetInitialised = true;
            return;
        }

        if (engineTarget != useSideband)
        {
            engineTarget = useSideband;
            engineXfade.setTarget (useSideband ? 1.0f : 0.0f);
        }
    }

    /** T29: global F0 broadcast (f0 / clarity / voiced), control rate
        (updated per block by MultibandEngine's F0Tracker). */
    void setF0Broadcast (float f0, float clarity, bool voiced) noexcept
    {
        f0Broadcast.f0 = f0;
        f0Broadcast.clarity = clarity;
        f0Broadcast.voiced = voiced;
    }

    /** Fans out the per-band parameter bundle. RT safe; all units smooth
        their gain-affecting parameters internally (no zipper noise). */
    void setParameters (const params::ParamSnapshot::Band& p) noexcept
    {
        PitchModUnit::Params pitchParams;
        pitchParams.jitterDepth = p.jitterDepth;
        pitchParams.jitterRate  = p.jitterRate;
        pitchParams.vibDepth    = p.vibDepth;
        pitchParams.vibRate     = p.vibRate;
        pitchParams.vibWave     = waveformFromInt (p.vibWave);
        pitchMod.setParameters (pitchParams);

        ShimmerUnit::Params shimmerParams;
        shimmerParams.depth = p.shimmerDepth;
        shimmerParams.rate  = p.shimmerRate;
        shimmer.setParameters (shimmerParams);

        HarmonicExciter::Params exciterParams;
        exciterParams.drive = p.harmonicDrive;
        exciterParams.tone  = p.harmonicTone;
        exciter.setParameters (exciterParams);

        // Sideband (T29): same per-band bundle plus the F0 broadcast. The
        // inactive engine still receives parameters so its smoothing stays
        // fresh for a later switch (REFACTOR_PLAN §2).
        SidebandEngine::Params sideParams;
        sideParams.f0        = f0Broadcast.f0;
        sideParams.clarity   = f0Broadcast.clarity;
        sideParams.voiced    = f0Broadcast.voiced;
        sideParams.depth     = p.sidebandDepth;
        sideParams.fmRatio   = p.sidebandFmRatio;
        sideParams.roughness = p.sidebandRoughness;
        sideParams.lowcut    = p.sidebandLowcut;
        sideParams.bright    = p.sidebandBright;   // T40 (orthogonal brightness)
        sideParams.enable    = p.sidebandEnable;
        sideband.setParameters (sideParams);

        // T49 whole-band fog (SPEC §4.3 / DECISIONS R44): independent of the
        // engine and of Sideband Rough; 0 = bit-transparent.
        BandFogUnit::Params fogParams;
        fogParams.amount = p.fog;
        bandFog.setParameters (fogParams);

        // Band output gain (SPEC §4.3): -48..+12 dB, smoothed in the dB
        // domain (perceptual), linearised per sample in process(). T71: the
        // -48 dB sentinel is the loudness knob's mute position; fade the
        // smoother towards internalMuteDb so the final gain reaches zero.
        bandGainSmooth.setTarget (params::loudness::dspTargetDb (p.gain));
        // T38 band master switch (dry passthrough when off).
        bandEnableSmooth.setTarget (p.enable ? 1.0f : 0.0f);

        // Audit P3: the smoothers start at unity/0 dB; a preset that loads
        // muted or with Band On off must not leak 5-20 ms of the wrong path
        // on the first block. The first post-prepare snapshot snaps.
        if (! parametersInitialised)
        {
            bandGainSmooth.snapTo (params::loudness::dspTargetDb (p.gain));
            bandEnableSmooth.snapTo (p.enable ? 1.0f : 0.0f);
            parametersInitialised = true;
        }
    }

    /** Seeds all random sources of the chain (tests / reproducibility). */
    void setSeed (std::int64_t seed) noexcept
    {
        pitchMod.setSeed (seed);
        shimmer.setSeed (static_cast<std::int64_t> (static_cast<std::uint64_t> (seed)
                                                    ^ 0x51F5A1E7C4B3D29Full)); // distinct stream
        sideband.setSeed (static_cast<std::int64_t> (static_cast<std::uint64_t> (seed)
                                                     ^ 0x5EED5EED5EED5EEDull)); // distinct stream
        bandFog.setSeed (static_cast<std::int64_t> (static_cast<std::uint64_t> (seed)
                                                    ^ 0xF060F060F060F060ull)); // T49 stream
    }

    /** Clears the audio history of the delay-based units (audit P2-3:
        structural band-count changes must not replay stale delay content).
        Smoothing/gain state is preserved; no reallocation. */
    void resetState() noexcept
    {
        pitchMod.resetState();
        exciter.resetState();
        sideband.reset();
        bandFog.reset();
        if (bypassDelay != nullptr)
            bypassDelay->reset();
    }

    /** Advances the whole chain by one sample. */
    float process (float x) noexcept
    {
        auto y = pitchMod.process (x);
        y = shimmer.process (y);

        // T29 dual engine: crossfade between legacy exciter and sideband.
        // Only the engine(s) actually contributing run (R9); during the 5 ms
        // fade both run so the mix is continuous (no click).
        const auto xf = engineXfade.getNext();   // 0 = Legacy, 1 = Sideband
        const auto legacy = (xf < 1.0f) ? exciter.process (y) : 0.0f;
        const auto sb     = (xf > 0.0f) ? sideband.process (y) : 0.0f;

        // T49/T50: apply the Band Effects Rough (internal fog) to the WHOLE
        // band after the engine mix (Legacy and Sideband alike, independent of
        // Sideband depth/Rough). At Rough = 0 this is a bit-exact passthrough
        // and only advances its delay line.
        const auto engineOut = legacy * (1.0f - xf) + sb * xf;
        const auto out = bandFog.process (engineOut);

        // T38 band master switch: off = dry passthrough (all effects bypass),
        // crossfaded over 5 ms to stay click-free. T45: the dry is delayed by
        // the fixed sideband baseline (blended with the engine crossfade for
        // Legacy) so Band On/off cannot change the output latency.
        const auto eb = bandEnableSmooth.getNext();
        const auto delayedDry = bypassDelay->process (x, bypassDelaySamples);
        const auto alignedDry = x * (1.0f - xf) + delayedDry * xf;
        const auto gainDb = bandGainSmooth.getNext();
        const auto bandGain = gainDb <= params::loudness::muteThresholdDb
                                  ? 0.0f
                                  : juce::Decibels::decibelsToGain (gainDb);
        const auto mixed = (alignedDry * (1.0f - eb) + out * eb) * bandGain;

        return mixed;
    }

    //==========================================================================
    // Diagnostics (tests).
    float getJitterAmpSamples() const noexcept { return pitchMod.getJitterAmpSamples(); }
    float getVibAmpSamples() const noexcept    { return pitchMod.getVibAmpSamples(); }
    float getLastShimmerGain() const noexcept  { return shimmer.getLastGain(); }
    float getBandFogLastPhaseRadians() const noexcept { return bandFog.getLastPhaseRadians(); } // T58
    float getBandFogAmount() const noexcept           { return bandFog.getFogAmount(); }         // T49

    // T29: engine crossfade ramp length (REFACTOR_PLAN §3.2).
    static constexpr float engineCrossfadeMs = 5.0f;

private:
    static ModLFO::Waveform waveformFromInt (int wave) noexcept
    {
        switch (wave)
        {
            case 1:  return ModLFO::Waveform::square;
            case 2:  return ModLFO::Waveform::fullWave;
            case 3:  return ModLFO::Waveform::noise;
            default: return ModLFO::Waveform::sine; // 0 and any corrupt value
        }
    }

    //==========================================================================
    PitchModUnit pitchMod;
    ShimmerUnit shimmer;
    HarmonicExciter exciter;
    SidebandEngine sideband;   // T29
    BandFogUnit bandFog;       // T49 whole-band fog

    // T29: engine selector crossfade (0 = Legacy, 1 = Sideband)
    util::SmoothedValue engineXfade;
    util::SmoothedValue bandGainSmooth;   // dB, -48..+12 (SPEC §4.3)
    util::SmoothedValue bandEnableSmooth; // 1 = effects on, 0 = dry bypass (T38)
    bool engineTarget = false;
    bool engineTargetInitialised = false;
    bool parametersInitialised = false; // audit P3: first snapshot snaps gain/enable

    // T45: dry path for the Band On bypass; holds the fixed sideband baseline.
    std::unique_ptr<ModDelayLine> bypassDelay;
    float bypassDelaySamples = 0.0f;

    // T29: global F0 broadcast (control rate, from MultibandEngine::F0Tracker)
    struct F0Broadcast { float f0 = 0.0f; float clarity = 0.0f; bool voiced = false; };
    F0Broadcast f0Broadcast;
};

} // namespace dsp
