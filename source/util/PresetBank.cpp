#include "util/PresetBank.h"

namespace util
{

//==============================================================================
//  Factory presets (voice-oriented).  Physical values, all inside the SPEC §4
//  ranges asserted by ParameterLayoutTests.
//==============================================================================
namespace
{

using Category = PresetBank::Category;

const PresetBank::PresetParam cleanVoice[] =
{
    { "formant_enabled",     0.0f }, // M2: switching to a non-formant preset must turn the path off
    { "formant_shift",       0.0f },
    { "formant_quefrency",   1.0f },
    { "dry_wet",           100.0f },
    { "output_trim",         0.0f },
    { "bypass",              0.0f },
    { "engine_version",      0.0f }, // T30: explicit engine choice, load-order independent (R18)
};

const PresetBank::PresetParam warmVoice[] =
{
    { "formant_enabled",     1.0f }, // M2: without this the formant path stays off
    { "formant_shift",      -1.5f },
    { "dry_wet",           100.0f },
    { "b1_harmonic_drive", 25.0f },
    { "b1_harmonic_tone", 2200.0f },
    { "b1_jitter_depth",    8.0f },
    { "b1_jitter_rate",   150.0f },
    { "engine_version",      0.0f }, // T30: explicit engine choice, load-order independent (R18)
};

const PresetBank::PresetParam radioVoice[] =
{
    { "formant_enabled",     0.0f }, // M2: formant path stays off
    { "formant_quefrency",   2.0f },
    { "band_count",          2.0f }, // index 2 = 3 bands: b2 is the 200-4000 Hz mid band
    { "dry_wet",           100.0f },
    { "b2_jitter_depth",   25.0f },
    { "b2_jitter_rate",   100.0f },
    { "b2_harmonic_drive", 30.0f },
    { "b2_harmonic_tone", 1800.0f },
    { "engine_version",      0.0f }, // T30: explicit engine choice, load-order independent (R18)
};

const PresetBank::PresetParam loFiTape[] =
{
    { "formant_enabled",     0.0f }, // M2: formant path stays off
    { "band_count",          2.0f }, // index 2 = 3 bands: b3 is the >4 kHz high band
    { "dry_wet",            90.0f },
    { "b3_shimmer_depth",   45.0f },
    { "b3_shimmer_rate",   100.0f },
    { "b3_jitter_depth",    30.0f },
    { "b3_jitter_rate",   120.0f },
    { "b3_vib_depth",       15.0f },
    { "b3_vib_rate",        45.0f },
    { "engine_version",      0.0f }, // T30: explicit engine choice, load-order independent (R18)
};

const PresetBank::PresetParam pitchUp[] =
{
    { "formant_enabled",     1.0f }, // M2: the shift must actually reach the engine
    { "formant_shift",       4.0f },
    { "formant_quefrency",   1.5f },
    { "engine_version",      0.0f }, // T30: explicit engine choice, load-order independent (R18)
};

const PresetBank::PresetParam pitchDown[] =
{
    { "formant_enabled",     1.0f }, // M2
    { "formant_shift",      -4.5f },
    { "formant_quefrency",   0.8f },
    { "engine_version",      0.0f }, // T30: explicit engine choice, load-order independent (R18)
};

const PresetBank::PresetParam brightExciter[] =
{
    { "formant_enabled",     0.0f }, // M2: formant path stays off
    { "band_count",          2.0f }, // index 2 = 3 bands (b1 + b3)
    { "dry_wet",            85.0f },
    { "b1_harmonic_drive",  20.0f },
    { "b1_harmonic_tone",  4000.0f },
    { "b3_harmonic_drive",  55.0f },
    { "b3_harmonic_tone",  6500.0f },
    { "engine_version",      0.0f }, // T30: explicit engine choice, load-order independent (R18)
};

const PresetBank::PresetParam vintageVib[] =
{
    { "formant_enabled",     0.0f }, // M2: formant path stays off
    { "dry_wet",           100.0f },
    { "b1_vib_depth",       18.0f },
    { "b1_vib_rate",        55.0f },
    { "b1_vib_wave",         0.0f },
    { "b1_jitter_depth",    12.0f },
    { "b1_jitter_rate",   120.0f },
    { "engine_version",      0.0f }, // T30: explicit engine choice, load-order independent (R18)
};

//  T67: Growl family — Legacy band modulation (T61) in three intensities.
//  Growl 2 is the original T61 preset; 1/3 scale the same character down/up.
//  The sidebands come from PitchModUnit (b2 Sine 128 Hz plus Jitter) and the
//  >4 kHz fog from BandFogUnit (b3 Noise + Jitter + Rough + Gain). The
//  SidebandEngine is not used; depth-0 would only add its Lmax path/PDC.
const PresetBank::PresetParam growl1[] =
{
    { "formant_enabled",     0.0f },
    { "band_count",          2.0f }, // 3 bands: b1 <1k, b2 1-4k, b3 >4k
    { "xover1_freq",      1000.0f },
    { "xover2_freq",      4000.0f },
    { "xover_slope",         1.0f }, // 24 dB/oct
    { "dry_wet",           100.0f },
    { "bypass",              0.0f },
    { "engine_version",      0.0f }, // Legacy

    { "b1_vib_wave",         0.0f }, // Sine
    { "b1_vib_rate",       128.0f },
    { "b1_vib_depth",       80.0f },
    { "b1_jitter_rate",    500.0f },
    { "b1_jitter_depth",   150.0f },

    { "b2_vib_wave",         0.0f }, // Sine: keeps the identifiable 128 Hz spacing
    { "b2_vib_rate",       128.0f },
    { "b2_vib_depth",       80.0f },
    { "b2_jitter_rate",    300.0f },
    { "b2_jitter_depth",   220.0f },

    { "b3_vib_wave",         3.0f }, // Noise
    { "b3_vib_rate",       200.0f },
    { "b3_vib_depth",      100.0f },
    { "b3_jitter_rate",    450.0f },
    { "b3_jitter_depth",   160.0f },
    { "b3_fog",             18.0f }, // Band Effects Rough
    { "b3_gain",             4.0f }, // dB
};

const PresetBank::PresetParam growl2[] =
{
    { "formant_enabled",     0.0f },
    { "band_count",          2.0f }, // 3 bands: b1 <1k, b2 1-4k, b3 >4k
    { "xover1_freq",      1000.0f },
    { "xover2_freq",      4000.0f },
    { "xover_slope",         1.0f }, // 24 dB/oct
    { "dry_wet",           100.0f },
    { "bypass",              0.0f },
    { "engine_version",      0.0f }, // Legacy; presets own their engine (R18)

    { "b1_vib_wave",         0.0f }, // Sine
    { "b1_vib_rate",       128.0f },
    { "b1_vib_depth",      120.0f },
    { "b1_jitter_rate",    500.0f },
    { "b1_jitter_depth",   250.0f },

    { "b2_vib_wave",         0.0f }, // Sine: keeps the identifiable 128 Hz spacing
    { "b2_vib_rate",       128.0f },
    { "b2_vib_depth",      120.0f },
    { "b2_jitter_rate",    320.0f },
    { "b2_jitter_depth",   375.0f },

    { "b3_vib_wave",         3.0f }, // Noise
    { "b3_vib_rate",       200.0f },
    { "b3_vib_depth",      150.0f },
    { "b3_jitter_rate",    500.0f },
    { "b3_jitter_depth",   250.0f },
    { "b3_fog",             30.0f }, // Band Effects Rough
    { "b3_gain",             6.0f }, // dB
};

const PresetBank::PresetParam growl3[] =
{
    { "formant_enabled",     0.0f },
    { "band_count",          2.0f }, // 3 bands: b1 <1k, b2 1-4k, b3 >4k
    { "xover1_freq",      1000.0f },
    { "xover2_freq",      4000.0f },
    { "xover_slope",         1.0f }, // 24 dB/oct
    { "dry_wet",           100.0f },
    { "bypass",              0.0f },
    { "engine_version",      0.0f }, // Legacy

    { "b1_vib_wave",         0.0f }, // Sine
    { "b1_vib_rate",       128.0f },
    { "b1_vib_depth",      150.0f },
    { "b1_jitter_rate",    550.0f },
    { "b1_jitter_depth",   350.0f },

    { "b2_vib_wave",         0.0f }, // Sine: keeps the identifiable 128 Hz spacing
    { "b2_vib_rate",       128.0f },
    { "b2_vib_depth",      150.0f },
    { "b2_jitter_rate",    350.0f },
    { "b2_jitter_depth",   500.0f },

    { "b3_vib_wave",         3.0f }, // Noise
    { "b3_vib_rate",       200.0f },
    { "b3_vib_depth",      190.0f },
    { "b3_jitter_rate",    600.0f },
    { "b3_jitter_depth",   320.0f },
    { "b3_fog",             45.0f }, // Band Effects Rough
    { "b3_gain",             7.0f }, // dB
};

//  T67: Anger family — the accepted N3 global growl (T66) in three
//  intensities. Anger 2 is the exact N3 preset; 1/3 scale the global Amount
//  and the Legacy bed down/up. Rate 0.15 + Tone +25 make 3 slower and
//  darker; no automatic makeup (Output Trim compensates the RMS drop).
const PresetBank::PresetParam anger1[] =
{
    { "formant_enabled",     0.0f },
    { "band_count",          2.0f }, // 3 bands: the N3 prototype bed
    { "xover1_freq",      1000.0f },
    { "xover2_freq",      4000.0f },
    { "xover_slope",         1.0f }, // 24 dB/oct
    { "dry_wet",           100.0f },
    { "bypass",              0.0f },
    { "engine_version",      0.0f }, // Legacy
    { "b1_vib_wave",         0.0f }, // Sine
    { "b1_vib_rate",       100.0f },
    { "b1_vib_depth",       60.0f },
    { "b1_jitter_rate",    300.0f },
    { "b1_jitter_depth",    60.0f },
    { "b2_vib_wave",         0.0f }, // Sine
    { "b2_vib_rate",       100.0f },
    { "b2_vib_depth",       60.0f },
    { "b2_jitter_rate",    300.0f },
    { "b2_jitter_depth",    60.0f },
    { "b3_vib_wave",         3.0f }, // Noise
    { "b3_vib_rate",       200.0f },
    { "b3_vib_depth",      150.0f },
    { "b3_jitter_rate",    500.0f },
    { "b3_jitter_depth",   250.0f },
    { "b3_fog",              6.0f },
    { "b3_gain",             1.0f },
    { "growl_amount",       60.0f },
    { "growl_rate",          0.20f },
    { "growl_tone",          0.0f },
    { "output_trim",         1.0f },
};

const PresetBank::PresetParam anger2[] =
{
    { "formant_enabled",     0.0f },
    { "band_count",          2.0f }, // 3 bands: the N3 prototype bed
    { "xover1_freq",      1000.0f },
    { "xover2_freq",      4000.0f },
    { "xover_slope",         1.0f }, // 24 dB/oct
    { "dry_wet",           100.0f },
    { "bypass",              0.0f },
    { "engine_version",      0.0f }, // Legacy
    { "b1_vib_wave",         0.0f }, // Sine
    { "b1_vib_rate",       100.0f },
    { "b1_vib_depth",       80.0f },
    { "b1_jitter_rate",    300.0f },
    { "b1_jitter_depth",    80.0f },
    { "b2_vib_wave",         0.0f }, // Sine
    { "b2_vib_rate",       100.0f },
    { "b2_vib_depth",       80.0f },
    { "b2_jitter_rate",    300.0f },
    { "b2_jitter_depth",    80.0f },
    { "b3_vib_wave",         3.0f }, // Noise
    { "b3_vib_rate",       200.0f },
    { "b3_vib_depth",      150.0f },
    { "b3_jitter_rate",    500.0f },
    { "b3_jitter_depth",   250.0f },
    { "b3_fog",             10.0f },
    { "b3_gain",             2.0f },
    { "growl_amount",      100.0f },
    { "growl_rate",          0.20f },
    { "growl_tone",          0.0f },
    { "output_trim",         2.0f },
};

const PresetBank::PresetParam anger3[] =
{
    { "formant_enabled",     0.0f },
    { "band_count",          2.0f }, // 3 bands: the N3 prototype bed
    { "xover1_freq",      1000.0f },
    { "xover2_freq",      4000.0f },
    { "xover_slope",         1.0f }, // 24 dB/oct
    { "dry_wet",           100.0f },
    { "bypass",              0.0f },
    { "engine_version",      0.0f }, // Legacy
    { "b1_vib_wave",         0.0f }, // Sine
    { "b1_vib_rate",       100.0f },
    { "b1_vib_depth",      180.0f },
    { "b1_jitter_rate",    300.0f },
    { "b1_jitter_depth",   180.0f },
    { "b2_vib_wave",         0.0f }, // Sine
    { "b2_vib_rate",       100.0f },
    { "b2_vib_depth",      180.0f },
    { "b2_jitter_rate",    300.0f },
    { "b2_jitter_depth",   180.0f },
    { "b3_vib_wave",         3.0f }, // Noise
    { "b3_vib_rate",       200.0f },
    { "b3_vib_depth",      150.0f },
    { "b3_jitter_rate",    500.0f },
    { "b3_jitter_depth",   250.0f },
    { "b3_fog",             22.0f },
    { "b3_gain",             4.0f },
    { "growl_amount",      100.0f },
    { "growl_rate",          0.15f },
    { "growl_tone",         25.0f },
    { "output_trim",         2.0f },
};

//==============================================================================
//  Sideband presets (T35, Phase 6 showcase). Both declare engine_version=1
//  explicitly (R18: presets own their engine choice) and keep every value
//  inside the SPEC §4.4 ranges asserted by ParameterLayoutTests. The legacy
//  path is untouched; the 3-band roughness stack differs only in depth/
//  roughness/fm_ratio so the pair demonstrates the texture range.
//==============================================================================
const PresetBank::PresetParam roughAir[] =
{
    { "formant_enabled",     0.0f }, // M2: formant path stays off
    { "band_count",          2.0f }, // index 2 = 3 bands (the preset configures b1..b3)
    { "dry_wet",           100.0f },
    { "engine_version",      1.0f }, // T35: Sideband engine (Phase 6 default)
    { "b1_sideband_enable",   1.0f },
    { "b1_sideband_depth",   55.0f },
    { "b1_sideband_fm_ratio", 0.27f },
    { "b1_sideband_roughness", 40.0f },
    { "b1_sideband_lowcut",  300.0f },
    { "b2_sideband_enable",   1.0f },
    { "b2_sideband_depth",   45.0f },
    { "b2_sideband_fm_ratio", 0.27f },
    { "b2_sideband_roughness", 30.0f },
    { "b2_sideband_lowcut",  300.0f },
    { "b3_sideband_enable",   1.0f },
    { "b3_sideband_depth",   50.0f },
    { "b3_sideband_fm_ratio", 0.27f },
    { "b3_sideband_roughness", 50.0f },
    { "b3_sideband_lowcut",  300.0f },
};

const PresetBank::PresetParam sidebandGrit[] =
{
    { "formant_enabled",     0.0f }, // M2: formant path stays off
    { "band_count",          2.0f }, // index 2 = 3 bands (the preset configures b1..b3)
    { "dry_wet",           100.0f },
    { "engine_version",      1.0f }, // T35: Sideband engine (Phase 6 default)
    { "b1_sideband_enable",   1.0f },
    { "b1_sideband_depth",  100.0f },
    { "b1_sideband_fm_ratio", 0.40f },
    { "b1_sideband_roughness", 100.0f },
    { "b1_sideband_lowcut",  100.0f },
    { "b2_sideband_enable",   1.0f },
    { "b2_sideband_depth",   90.0f },
    { "b2_sideband_fm_ratio", 0.40f },
    { "b2_sideband_roughness", 100.0f },
    { "b2_sideband_lowcut",  100.0f },
    { "b3_sideband_enable",   1.0f },
    { "b3_sideband_depth",  100.0f },
    { "b3_sideband_fm_ratio", 0.40f },
    { "b3_sideband_roughness", 100.0f },
    { "b3_sideband_lowcut",  100.0f },
};

// T40: "Steel Core" — the independent brightness axis in action. Bright is
// raised on the presence band (b2 owns the 2–4 kHz core at the default
// 200/4000 Hz crossovers) and on the air band (b3); roughness stays LOW so
// the result is a sharp, metallic edge rather than the fog of Rough Air /
// Sideband Grit. b1's wet path sits below the 1.5 kHz tilt, so its Bright is
// deliberately mild (documented in the user manual).
const PresetBank::PresetParam steelCore[] =
{
    { "formant_enabled",     0.0f }, // M2: formant path stays off
    { "band_count",          2.0f }, // 3 bands: Bright lives on b2 (presence) + b3 (air)
    { "dry_wet",           100.0f },
    { "engine_version",      1.0f }, // Sideband engine (R18: presets own the engine)
    { "b1_sideband_enable",   1.0f },
    { "b1_sideband_depth",   40.0f },
    { "b1_sideband_fm_ratio", 0.35f },
    { "b1_sideband_roughness", 20.0f },
    { "b1_sideband_lowcut",  100.0f },
    { "b1_sideband_bright",   50.0f },
    { "b2_sideband_enable",   1.0f },
    { "b2_sideband_depth",   60.0f },
    { "b2_sideband_fm_ratio", 0.37f },
    { "b2_sideband_roughness", 30.0f },
    { "b2_sideband_lowcut",  500.0f },
    { "b2_sideband_bright",   80.0f },
    { "b3_sideband_enable",   1.0f },
    { "b3_sideband_depth",   70.0f },
    { "b3_sideband_fm_ratio", 0.40f },
    { "b3_sideband_roughness", 35.0f },
    { "b3_sideband_lowcut",  800.0f },
    { "b3_sideband_bright",   85.0f },
};

const PresetBank::Preset presets[PresetBank::numPresets] =
{
    { "Clean Voice",     Category::soundEffects, cleanVoice,     (int) (sizeof (cleanVoice)     / sizeof (cleanVoice[0])) },
    { "Warm Voice",      Category::soundEffects, warmVoice,      (int) (sizeof (warmVoice)      / sizeof (warmVoice[0])) },
    { "Radio Voice",     Category::soundEffects, radioVoice,     (int) (sizeof (radioVoice)     / sizeof (radioVoice[0])) },
    { "Lo-Fi Tape",      Category::soundEffects, loFiTape,       (int) (sizeof (loFiTape)       / sizeof (loFiTape[0])) },
    { "Pitch Up",        Category::soundEffects, pitchUp,        (int) (sizeof (pitchUp)        / sizeof (pitchUp[0])) },
    { "Pitch Down",      Category::soundEffects, pitchDown,      (int) (sizeof (pitchDown)      / sizeof (pitchDown[0])) },
    { "Bright Exciter",  Category::soundEffects, brightExciter,  (int) (sizeof (brightExciter)  / sizeof (brightExciter[0])) },
    { "Vintage Vib",     Category::soundEffects, vintageVib,     (int) (sizeof (vintageVib)     / sizeof (vintageVib[0])) },
    { "Rough Air",       Category::soundEffects, roughAir,       (int) (sizeof (roughAir)       / sizeof (roughAir[0])) },
    { "Sideband Grit",   Category::soundEffects, sidebandGrit,   (int) (sizeof (sidebandGrit)   / sizeof (sidebandGrit[0])) },
    { "Steel Core",      Category::soundEffects, steelCore,      (int) (sizeof (steelCore)      / sizeof (steelCore[0])) },
    { "Growl 1",         Category::voiceDrive,   growl1,         (int) (sizeof (growl1)         / sizeof (growl1[0])) },
    { "Growl 2",         Category::voiceDrive,   growl2,         (int) (sizeof (growl2)         / sizeof (growl2[0])) },
    { "Growl 3",         Category::voiceDrive,   growl3,         (int) (sizeof (growl3)         / sizeof (growl3[0])) },
    { "Anger 1",         Category::voiceDrive,   anger1,         (int) (sizeof (anger1)         / sizeof (anger1[0])) },
    { "Anger 2",         Category::voiceDrive,   anger2,         (int) (sizeof (anger2)         / sizeof (anger2[0])) },
    { "Anger 3",         Category::voiceDrive,   anger3,         (int) (sizeof (anger3)         / sizeof (anger3[0])) },
};

//==============================================================================
void applyParam (juce::AudioProcessorValueTreeState& apvts, const PresetBank::PresetParam& p) noexcept
{
    auto* param = apvts.getParameter (p.id);
    if (param == nullptr)
        return;

    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (param))
    {
        const auto& range = f->getNormalisableRange();
        *f = juce::jlimit (range.start, range.end, p.value);
    }
    else if (auto* b = dynamic_cast<juce::AudioParameterBool*> (param))
    {
        *b = p.value > 0.5f;
    }
    else if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (param))
    {
        const auto index = static_cast<int> (p.value);
        *c = juce::jlimit (0, c->choices.size() - 1, index);
    }
}

} // namespace

//==============================================================================
const PresetBank::Preset* PresetBank::getPreset (int index) noexcept
{
    return (index >= 0 && index < numPresets) ? &presets[index] : nullptr;
}

juce::StringRef PresetBank::getPresetName (int index) noexcept
{
    const auto* preset = getPreset (index);
    return preset != nullptr ? juce::StringRef (preset->name) : juce::StringRef();
}

PresetBank::Category PresetBank::getPresetCategory (int index) noexcept
{
    const auto* preset = getPreset (index);
    return preset != nullptr ? preset->category : Category::soundEffects;
}

juce::StringRef PresetBank::getCategoryName (Category category) noexcept
{
    return category == Category::voiceDrive ? juce::StringRef ("Voice Drive")
                                            : juce::StringRef ("Sound Effects");
}

void PresetBank::loadPreset (juce::AudioProcessorValueTreeState& apvts, int index) noexcept
{
    const auto* preset = getPreset (index);
    if (preset == nullptr)
        return;

    // Factory presets are complete snapshots: start from the parameter defaults
    // so parameters not listed by the preset cannot leak in from the previous
    // state (e.g. Bypass, band_count, jitter/shimmer/sideband depths, Bright).
    // This also makes "Clean Voice" genuinely clean and keeps the result
    // independent of the load order.
    for (auto* param : apvts.processor.getParameters())
        if (param != nullptr)
            param->setValueNotifyingHost (param->getDefaultValue());

    for (int i = 0; i < preset->numParams; ++i)
        applyParam (apvts, preset->params[i]);
}

} // namespace util
