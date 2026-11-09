#pragma once

/*
    Real-time parameter snapshot (T04).

    A plain POD struct holding every parameter in *denormalised* (real-world)
    form, filled once per audio block from the APVTS atomics. Engines read
    ONLY this snapshot during processBlock — the single parameter source on
    the audio thread (ARCHITECTURE §6/§7.3).

    JUCE 9 note: AudioProcessorValueTreeState::getRawParameterValue() returns
    the denormalised value (verified in T02): float params carry their real
    value (Hz, %, dB...), choice params carry the selected index, bool params
    carry 0.0f/1.0f.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

#include "ParameterIDs.h"

namespace params
{

struct ParamSnapshot
{
    //==========================================================================
    //  Per-band snapshot (SPEC §4.3)
    //==========================================================================
    struct Band
    {
        float jitterDepth   = 0.0f;   // cents
        float jitterRate    = 0.0f;   // Hz
        float shimmerDepth  = 0.0f;   // %
        float shimmerRate   = 0.0f;   // Hz
        float harmonicDrive = 0.0f;   // %
        float harmonicTone  = 0.0f;   // Hz
        int   vibWave       = 2;      // 0=Sine 1=Square 2=FullWave 3=Noise (default FullWave)
        float vibRate       = 0.0f;   // Hz
        float vibDepth      = 0.0f;   // cents
        float gain          = 0.0f;   // dB, -48..+12 (SPEC §4.3 band gain)
        bool  enable        = true;   // band master switch (T38, SPEC §4.3)
        // Sideband (SIDEBAND_SPEC §4.2)
        bool  sidebandEnable    = true;  // off/on (default on, C4)
        float sidebandDepth     = 0.0f;  // % (0 = fully transparent)
        float sidebandFmRatio   = 0.333f;// fm / f0 (SPEC §4.3 default 1/3)
        float sidebandRoughness = 20.0f; // %
        float sidebandLowcut    = 2000.0f;// Hz (SPEC §4.3 default 2000)
        float sidebandBright    = 0.0f;  // % T40 independent brightness axis (default 0)
        float fog               = 0.0f;  // % T49 whole-band fog (default 0 = transparent)
    };

    //==========================================================================
    //  Global parameters (SPEC §4.2)
    //==========================================================================
    bool  bypass          = false;   // off/on
    bool  formantEnabled  = false;   // off/on
    int   bandCount       = 1;       // 1/2/3 (SPEC §4.2 default 1)
    int   xoverSlope      = 24;      // 12/24/36/48/60/72 dB/oct
    float xover1Freq      = 200.0f;  // Hz
    float xover2Freq      = 4000.0f; // Hz (SPEC §4.2 default 4000)
    float formantShift    = 0.0f;    // semitones
    float formantQuefrency = 0.5f;   // ms
    float dryWet          = 100.0f;  // %
    float outputTrim      = 0.0f;    // dB
    int   engineVersion   = 0;       // 0=Legacy 1=Sideband (SIDEBAND_SPEC §4.1)

    // T66 global Growl (SPEC §4.2)
    float growlAmount     = 0.0f;    // % (0 = bit-transparent)
    float growlRate       = 0.20f;   // fm / f0
    float growlTone       = 0.0f;    // -100..+100 (0 = N3 de-harsh)

    Band bands[numBands];

    //==========================================================================
    //  Fill from the APVTS atomics — one atomic load per parameter. Must be
    //  called at the start of each processBlock (audio thread only).
    //
    //  RT contract: ZERO heap allocation. All parameter IDs used here are
    //  compile-time literals (see IDs::b{n}_* in ParameterIDs.h); no
    //  juce::String is ever constructed on this path.
    //==========================================================================
    void fillFrom (const juce::AudioProcessorValueTreeState& apvts) noexcept
    {
        auto load = [&] (juce::StringRef id)
        {
            auto* ptr = apvts.getRawParameterValue (id);
            jassert (ptr != nullptr); // parameter must exist in the layout
            const auto value = ptr != nullptr ? ptr->load() : 0.0f;
            // Audit P3: a corrupted state/custom XML must not inject NaN/Inf
            // into the DSP graph; fall back to 0 (a valid, clamped value).
            return std::isfinite (value) ? value : 0.0f;
        };

        bypass         = load (IDs::bypass) > 0.5f;
        formantEnabled = load (IDs::formantEnabled) > 0.5f;

        // Choice params: the denormalised raw value is the *index*; store the
        // SPEC-meaningful values instead (bandCount 1/2/3, xoverSlope dB/oct).
        bandCount      = static_cast<int> (std::lround (load (IDs::bandCount))) + 1;
        xoverSlope     = slopeValues[juce::jlimit (0, 5, static_cast<int> (std::lround (load (IDs::xoverSlope))))];

        xover1Freq     = load (IDs::xover1Freq);
        xover2Freq     = load (IDs::xover2Freq);
        formantShift   = load (IDs::formantShift);
        formantQuefrency = load (IDs::formantQuefrency);
        dryWet         = load (IDs::dryWet);
        outputTrim     = load (IDs::outputTrim);
        engineVersion  = juce::jlimit (0, 1, static_cast<int> (std::lround (load (IDs::engineVersion))));

        // T66 global Growl
        growlAmount    = load (IDs::growlAmount);
        growlRate      = load (IDs::growlRate);
        growlTone      = load (IDs::growlTone);

        for (int band = 0; band < numBands; ++band)
        {
            const auto& id = bandParameterIDs[band];
            auto& b = bands[band];

            b.jitterDepth   = load (id.jitterDepth);
            b.jitterRate    = load (id.jitterRate);
            b.shimmerDepth  = load (id.shimmerDepth);
            b.shimmerRate   = load (id.shimmerRate);
            b.harmonicDrive = load (id.harmonicDrive);
            b.harmonicTone  = load (id.harmonicTone);

            // vibWave keeps the raw choice index (0=Sine 1=Square 2=FullWave 3=Noise).
            b.vibWave       = static_cast<int> (std::lround (load (id.vibWave)));

            b.vibRate       = load (id.vibRate);
            b.vibDepth      = load (id.vibDepth);
            b.gain          = load (id.gain);   // dB, -48..+12
            b.enable        = load (id.enable) > 0.5f;

            b.sidebandEnable    = load (id.sidebandEnable) > 0.5f;
            b.sidebandDepth     = load (id.sidebandDepth);
            b.sidebandFmRatio   = load (id.sidebandFmRatio);
            b.sidebandRoughness = load (id.sidebandRoughness);
            b.sidebandLowcut    = load (id.sidebandLowcut);
            b.sidebandBright    = load (id.sidebandBright);   // T40
            b.fog               = load (id.fog);              // T49
        }
    }

private:
    // SPEC §4.2 xover_slope choices: index -> dB/oct
    static constexpr int slopeValues[6] = { 12, 24, 36, 48, 60, 72 };

    // Compile-time ID table per band (SPEC §4.3 + SIDEBAND_SPEC §4.2),
    // indexed 0..2 -> b1/b2/b3.
    struct BandIDs
    {
        const char* jitterDepth;
        const char* jitterRate;
        const char* shimmerDepth;
        const char* shimmerRate;
        const char* harmonicDrive;
        const char* harmonicTone;
        const char* vibWave;
        const char* vibRate;
        const char* vibDepth;
        const char* gain;
        const char* enable;
        const char* sidebandEnable;
        const char* sidebandDepth;
        const char* sidebandFmRatio;
        const char* sidebandRoughness;
        const char* sidebandLowcut;
        const char* sidebandBright;   // T40
        const char* fog;              // T49
    };

    static constexpr BandIDs bandParameterIDs[numBands]
    {
        { IDs::b1_jitter_depth,   IDs::b1_jitter_rate,    IDs::b1_shimmer_depth,  IDs::b1_shimmer_rate,
          IDs::b1_harmonic_drive, IDs::b1_harmonic_tone,  IDs::b1_vib_wave,       IDs::b1_vib_rate,   IDs::b1_vib_depth,
          IDs::b1_gain, IDs::b1_enable,
          IDs::b1_sideband_enable, IDs::b1_sideband_depth, IDs::b1_sideband_fm_ratio,
          IDs::b1_sideband_roughness, IDs::b1_sideband_lowcut, IDs::b1_sideband_bright,
          IDs::b1_fog },
        { IDs::b2_jitter_depth,   IDs::b2_jitter_rate,    IDs::b2_shimmer_depth,  IDs::b2_shimmer_rate,
          IDs::b2_harmonic_drive, IDs::b2_harmonic_tone,  IDs::b2_vib_wave,       IDs::b2_vib_rate,   IDs::b2_vib_depth,
          IDs::b2_gain, IDs::b2_enable,
          IDs::b2_sideband_enable, IDs::b2_sideband_depth, IDs::b2_sideband_fm_ratio,
          IDs::b2_sideband_roughness, IDs::b2_sideband_lowcut, IDs::b2_sideband_bright,
          IDs::b2_fog },
        { IDs::b3_jitter_depth,   IDs::b3_jitter_rate,    IDs::b3_shimmer_depth,  IDs::b3_shimmer_rate,
          IDs::b3_harmonic_drive, IDs::b3_harmonic_tone,  IDs::b3_vib_wave,       IDs::b3_vib_rate,   IDs::b3_vib_depth,
          IDs::b3_gain, IDs::b3_enable,
          IDs::b3_sideband_enable, IDs::b3_sideband_depth, IDs::b3_sideband_fm_ratio,
          IDs::b3_sideband_roughness, IDs::b3_sideband_lowcut, IDs::b3_sideband_bright,
          IDs::b3_fog },
    };
};

static_assert (std::is_trivially_copyable_v<ParamSnapshot>,
               "ParamSnapshot must stay a plain POD for RT-safe copying");

} // namespace params
