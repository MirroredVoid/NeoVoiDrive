#pragma once

/*
    Parameter ID constants — mechanically mirror SPEC.md §4 (68 parameters:
    10 global + 3 × 11 legacy band (9 original + T37 gain + T38 enable)
    + 18 per-band sideband + engine_version + 3 × T49 band fog
    + 3 × T66 global Growl, SIDEBAND_SPEC §4).

    Constraint: IDs must match SPEC §4 word-for-word. Any parameter change must
    update SPEC first, then this file (ARCHITECTURE §7.1). The current legacy
    prefix is 43; all post-T37 parameters use versionHint 2 (IMPACT_ANALYSIS C5,
    DECISIONS R19/R41/R44).
*/

#include <juce_core/juce_core.h>

namespace params
{

// Number of bands / parameters (SPEC §4.3, §4.2; SIDEBAND_SPEC §4)
inline constexpr int numBands                = 3;
inline constexpr int numGlobalParams         = 10;
inline constexpr int numBandParams           = 11; // 10 + band enable (T38, SPEC §4.3)
inline constexpr int numSidebandBandParams   = 6; // per-band sideband (SIDEBAND_SPEC §4.2; T40 +bright)
inline constexpr int numSidebandGlobalParams = 1; // engine_version (SIDEBAND_SPEC §4.1)
inline constexpr int numBandFogParams        = 1; // T49 whole-band fog (SPEC §4.3)
inline constexpr int numGrowlParams          = 3; // T66 global Growl (appended after fog)
inline constexpr int numParameters = numGlobalParams + numBands * numBandParams
                                   + numBands * numSidebandBandParams + numSidebandGlobalParams
                                   + numBands * numBandFogParams + numGrowlParams; // 68

//==============================================================================
//  Global parameter IDs (SPEC §4.2)
//==============================================================================
namespace IDs
{
inline constexpr auto bypass           = "bypass";
inline constexpr auto bandCount        = "band_count";
inline constexpr auto xover1Freq       = "xover1_freq";
inline constexpr auto xover2Freq       = "xover2_freq";
inline constexpr auto xoverSlope       = "xover_slope";
inline constexpr auto formantEnabled   = "formant_enabled";
inline constexpr auto formantShift     = "formant_shift";
inline constexpr auto formantQuefrency = "formant_quefrency";
inline constexpr auto dryWet           = "dry_wet";
inline constexpr auto outputTrim       = "output_trim";
inline constexpr auto engineVersion    = "engine_version"; // SIDEBAND_SPEC §4.1; ParameterID(id, 2)

// T66 global Growl. Appended AFTER b3_fog in the layout so existing parameter
// indices stay stable (C5/R19); IDs live here with the other globals.
inline constexpr auto growlAmount      = "growl_amount";
inline constexpr auto growlRate        = "growl_rate";
inline constexpr auto growlTone        = "growl_tone";

//==============================================================================
//  Per-band parameter base names (SPEC §4.3); full ID = b{n}_{base}, {n} in b1/b2/b3
//==============================================================================
inline constexpr auto jitterDepth   = "jitter_depth";
inline constexpr auto jitterRate    = "jitter_rate";
inline constexpr auto shimmerDepth  = "shimmer_depth";
inline constexpr auto shimmerRate   = "shimmer_rate";
inline constexpr auto harmonicDrive = "harmonic_drive";
inline constexpr auto harmonicTone  = "harmonic_tone";
inline constexpr auto vibWave       = "vib_wave";
inline constexpr auto vibRate       = "vib_rate";
inline constexpr auto vibDepth      = "vib_depth";
inline constexpr auto gain          = "gain";     // band gain, -48..+12 dB
inline constexpr auto enable        = "enable";   // band master switch (T38)
inline constexpr auto fog           = "fog";      // T49 whole-band fog; UI name Rough (T50)
// Sideband base names (SIDEBAND_SPEC §4.2); full ID = b{n}_{base}
inline constexpr auto sidebandEnable    = "sideband_enable";
inline constexpr auto sidebandDepth     = "sideband_depth";
inline constexpr auto sidebandFmRatio   = "sideband_fm_ratio";
inline constexpr auto sidebandRoughness = "sideband_roughness";
inline constexpr auto sidebandLowcut    = "sideband_lowcut";
inline constexpr auto sidebandBright    = "sideband_bright"; // T40: independent brightness axis
//==============================================================================
//  Full per-band parameter IDs (SPEC §4.3): b1_/b2_/b3_ prefixed literals.
//  Compile-time constants so RT paths (ParamSnapshot::fillFrom) never build
//  juce::String objects. Must stay in sync with makeBandParameterID() — the
//  unit tests assert both agree.
//==============================================================================
inline constexpr auto b1_jitter_depth   = "b1_jitter_depth";
inline constexpr auto b1_jitter_rate    = "b1_jitter_rate";
inline constexpr auto b1_shimmer_depth  = "b1_shimmer_depth";
inline constexpr auto b1_shimmer_rate   = "b1_shimmer_rate";
inline constexpr auto b1_harmonic_drive = "b1_harmonic_drive";
inline constexpr auto b1_harmonic_tone  = "b1_harmonic_tone";
inline constexpr auto b1_vib_wave       = "b1_vib_wave";
inline constexpr auto b1_vib_rate       = "b1_vib_rate";
inline constexpr auto b1_vib_depth      = "b1_vib_depth";
inline constexpr auto b1_gain           = "b1_gain";
inline constexpr auto b1_enable         = "b1_enable";

inline constexpr auto b2_jitter_depth   = "b2_jitter_depth";
inline constexpr auto b2_jitter_rate    = "b2_jitter_rate";
inline constexpr auto b2_shimmer_depth  = "b2_shimmer_depth";
inline constexpr auto b2_shimmer_rate   = "b2_shimmer_rate";
inline constexpr auto b2_harmonic_drive = "b2_harmonic_drive";
inline constexpr auto b2_harmonic_tone  = "b2_harmonic_tone";
inline constexpr auto b2_vib_wave       = "b2_vib_wave";
inline constexpr auto b2_vib_rate       = "b2_vib_rate";
inline constexpr auto b2_vib_depth      = "b2_vib_depth";
inline constexpr auto b2_gain           = "b2_gain";
inline constexpr auto b2_enable         = "b2_enable";

inline constexpr auto b3_jitter_depth   = "b3_jitter_depth";
inline constexpr auto b3_jitter_rate    = "b3_jitter_rate";
inline constexpr auto b3_shimmer_depth  = "b3_shimmer_depth";
inline constexpr auto b3_shimmer_rate   = "b3_shimmer_rate";
inline constexpr auto b3_harmonic_drive = "b3_harmonic_drive";
inline constexpr auto b3_harmonic_tone  = "b3_harmonic_tone";
inline constexpr auto b3_vib_wave       = "b3_vib_wave";
inline constexpr auto b3_vib_rate       = "b3_vib_rate";
inline constexpr auto b3_vib_depth      = "b3_vib_depth";
inline constexpr auto b3_gain           = "b3_gain";
inline constexpr auto b3_enable         = "b3_enable";

//==============================================================================
//  Full per-band Band Effects IDs (T49, SPEC §4.3): b{n}_fog.
//  Registered at the END of the layout (after engine_version) to keep the
//  existing parameter indices stable (C5/R19); these constants keep the RT
//  snapshot path allocation-free.
//==============================================================================
inline constexpr auto b1_fog = "b1_fog";
inline constexpr auto b2_fog = "b2_fog";
inline constexpr auto b3_fog = "b3_fog";

//==============================================================================
//  Full per-band sideband parameter IDs (SIDEBAND_SPEC §4.2): b{n}_sideband_*.
//  Same RT-path contract as the legacy constants above.
//==============================================================================
inline constexpr auto b1_sideband_enable    = "b1_sideband_enable";
inline constexpr auto b1_sideband_depth     = "b1_sideband_depth";
inline constexpr auto b1_sideband_fm_ratio  = "b1_sideband_fm_ratio";
inline constexpr auto b1_sideband_roughness = "b1_sideband_roughness";
inline constexpr auto b1_sideband_lowcut    = "b1_sideband_lowcut";
inline constexpr auto b1_sideband_bright    = "b1_sideband_bright";

inline constexpr auto b2_sideband_enable    = "b2_sideband_enable";
inline constexpr auto b2_sideband_depth     = "b2_sideband_depth";
inline constexpr auto b2_sideband_fm_ratio  = "b2_sideband_fm_ratio";
inline constexpr auto b2_sideband_roughness = "b2_sideband_roughness";
inline constexpr auto b2_sideband_lowcut    = "b2_sideband_lowcut";
inline constexpr auto b2_sideband_bright    = "b2_sideband_bright";

inline constexpr auto b3_sideband_enable    = "b3_sideband_enable";
inline constexpr auto b3_sideband_depth     = "b3_sideband_depth";
inline constexpr auto b3_sideband_fm_ratio  = "b3_sideband_fm_ratio";
inline constexpr auto b3_sideband_roughness = "b3_sideband_roughness";
inline constexpr auto b3_sideband_lowcut    = "b3_sideband_lowcut";
inline constexpr auto b3_sideband_bright    = "b3_sideband_bright";
}

//==============================================================================
//  Full per-band parameter ID, e.g. makeBandParameterID (1, "jitter_depth")
//  -> "b1_jitter_depth" (SPEC §4.3).  Layout construction only — RT paths must
//  use the compile-time IDs::b{n}_* constants instead (zero allocation).
//==============================================================================
inline juce::String makeBandParameterID (int band, juce::StringRef base)
{
    jassert (band >= 1 && band <= numBands);
    return "b" + juce::String (band) + "_" + base;
}

} // namespace params
