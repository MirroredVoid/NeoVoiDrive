#include "ParameterLayout.h"

#include "LoudnessCurve.h"
#include "ParameterIDs.h"

#include <cmath>

namespace params
{

namespace
{

//==============================================================================
//  True logarithmic mapping: geometric midpoint sqrt(min*max) sits at
//  normalised 0.5, so slider/knob travel is proportional to log frequency.
//==============================================================================
juce::NormalisableRange<float> makeLogRange (float min, float max)
{
    return juce::NormalisableRange<float> (min, max,
        [min, max] (float, float, float v) { return min * std::pow (max / min, v); },
        [min, max] (float, float, float v) { return std::log (v / min) / std::log (max / min); },
        [] (float, float, float v) { return v; });
}

juce::AudioParameterFloatAttributes label (juce::String labelText)
{
    return juce::AudioParameterFloatAttributes().withLabel (std::move (labelText));
}

// Choice text per SPEC §4.2 / §4.3 / SIDEBAND_SPEC §4.1.
const juce::StringArray bandCountChoices { "1", "2", "3" };
const juce::StringArray slopeChoices     { "12", "24", "36", "48", "60", "72" };
const juce::StringArray vibWaveChoices   { "Sine", "Square", "FullWave", "Noise" };
const juce::StringArray engineChoices    { "Legacy (Harmonic Exciter)", "Sideband Roughness" };

} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ParameterLayout::create()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    //==========================================================================
    //  Global parameters (SPEC §4.2)
    //==========================================================================
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID (IDs::bypass, 1), "Bypass", false,
        juce::AudioParameterBoolAttributes().withLabel ("On/Off")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (IDs::bandCount, 1), "Bands", bandCountChoices, 0)); // default 1 band

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (IDs::xover1Freq, 1), "Crossover 1",
        makeLogRange (40.0f, 18000.0f), 200.0f, label ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (IDs::xover2Freq, 1), "Crossover 2",
        makeLogRange (40.0f, 18000.0f), 4000.0f, label ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (IDs::xoverSlope, 1), "Slope", slopeChoices, 1)); // default 24 dB/oct

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID (IDs::formantEnabled, 1), "Formant On", false,
        juce::AudioParameterBoolAttributes().withLabel ("On/Off")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (IDs::formantShift, 1), "Formant Shift",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, label ("st")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (IDs::formantQuefrency, 1), "Quefrency",
        makeLogRange (0.5f, 5.0f), 0.5f, label ("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (IDs::dryWet, 1), "Dry / Wet",
        juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f, label ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (IDs::outputTrim, 1), "Output Trim",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, label ("dB")));

    //==========================================================================
    //  Per-band parameters (SPEC §4.3), {n} in b1/b2/b3
    //==========================================================================
    // Audit P3: the host automation list shows every name; a per-band prefix
    // keeps the three "Jitter Depth"/"Band Gain"/... entries distinguishable.
    const auto bandName = [] (int band, const char* base)
    {
        return "Band " + juce::String (band) + " " + base;
    };

    for (int band = 1; band <= numBands; ++band)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::jitterDepth), 1), bandName (band, "Jitter Depth"),
            juce::NormalisableRange<float> (0.0f, 1200.0f), 0.0f, label ("ct")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::jitterRate), 1), bandName (band, "Jitter Rate"),
            makeLogRange (10.0f, 1000.0f), 100.0f, label ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::shimmerDepth), 1), bandName (band, "Shimmer Depth"),
            juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::shimmerRate), 1), bandName (band, "Shimmer Rate"),
            makeLogRange (10.0f, 1000.0f), 100.0f, label ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::harmonicDrive), 1), bandName (band, "Harmonic Drive"),
            juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::harmonicTone), 1), bandName (band, "Harmonic Tone"),
            makeLogRange (1000.0f, 10000.0f), 3000.0f, label ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (makeBandParameterID (band, IDs::vibWave), 1), bandName (band, "Vib Wave"),
            vibWaveChoices, 2)); // default FullWave

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::vibRate), 1), bandName (band, "Vib Rate"),
            makeLogRange (10.0f, 300.0f), 10.0f, label ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::vibDepth), 1), bandName (band, "Vib Depth"),
            juce::NormalisableRange<float> (0.0f, 200.0f), 0.0f, label ("ct")));

        // Band gain (SPEC §4.3): -48..+12 dB, default 0 dB — the spectrum
        // window's band-gain knobs; the envelope display follows it.
        // T71: custom normalisation — 50% = 0 dB, 100% = +12 dB, 0% = mute
        // (-48 dB sentinel), attenuation slow near center / fast near 0%.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::gain), 2), bandName (band, "Gain"),
            loudness::makeRange(), 0.0f, label ("dB")));

        // Band master switch (T38, SPEC §4.3): off = the whole band's effects
        // bypass (dry passthrough) and the card greys out.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (makeBandParameterID (band, IDs::enable), 2), bandName (band, "On"), true));
    }

    //==========================================================================
    //  Per-band sideband parameters (SIDEBAND_SPEC §4.2).
    //  APPENDED AFTER the legacy per-band loop — never inserted between legacy
    //  parameters (IMPACT_ANALYSIS C5 / DECISIONS R19: VST3 automation index
    //  compatibility). All new parameters carry versionHint 2.
    //==========================================================================
    for (int band = 1; band <= numBands; ++band)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (makeBandParameterID (band, IDs::sidebandEnable), 2), bandName (band, "SB On"), true,
            juce::AudioParameterBoolAttributes().withLabel ("On/Off")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::sidebandDepth), 2), bandName (band, "SB Depth"),
            juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::sidebandFmRatio), 2), bandName (band, "SB Ratio"),
            juce::NormalisableRange<float> (0.0f, 0.5f, 0.001f), 0.333f, label ("")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::sidebandRoughness), 2), bandName (band, "SB Rough"),
            juce::NormalisableRange<float> (0.0f, 100.0f), 20.0f, label ("%")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::sidebandLowcut), 2), bandName (band, "SB Low Cut"),
            makeLogRange (100.0f, 8000.0f), 2000.0f, label ("Hz")));

        // T40 (SPEC §4.3 / SIDEBAND_SPEC §10.1): independent brightness axis.
        // A per-band LINEAR tone stage on the wet path (1.5 kHz tilt + 3.2 kHz
        // presence bell) — orthogonal to roughness, default 0 = bit-transparent
        // and zero extra CPU (the stage is skipped while bright == 0).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::sidebandBright), 2), bandName (band, "SB Bright"),
            juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));
    }

    //==========================================================================
    //  Global engine selector (SIDEBAND_SPEC §4.1) — LAST parameter in the
    //  layout ([52], REFACTOR_PLAN §4.5). T35 flipped the default to Sideband;
    //  T62 (Sideband UI hidden) restores the Legacy default. The parameter
    //  stays registered so presets/automation can still select Sideband;
    //  v1 states are forced legacy on load (R20).
    //==========================================================================
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID (IDs::engineVersion, 2), "Engine", engineChoices, 0));

    //==========================================================================
    //  T49 whole-band fog (SPEC §4.3). Appended AFTER engine_version so the
    //  existing parameter indices stay stable (C5/R19). UI groups it with the
    //  Band Effects knobs; DSP applies it after the engine mix, so it works
    //  with both Legacy and Sideband and is independent of Sideband Rough.
    //==========================================================================
    for (int band = 1; band <= numBands; ++band)
    {
        // T50: the visible/host name is Rough; the ID stays b{n}_fog so
        // existing automation and saved states are unaffected.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (makeBandParameterID (band, IDs::fog), 2), bandName (band, "Rough"),
            juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));
    }

    //==========================================================================
    //  T66 global Growl (SPEC §4.2). Appended after the fog params so the
    //  existing indices stay stable (C5/R19). Amount=0 is bit-transparent.
    //  Rate is a ratio of the tracked F0; Tone is a bipolar de-harsh tilt.
    //==========================================================================
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (IDs::growlAmount, 2), "Growl Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f), 0.0f, label ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (IDs::growlRate, 2), "Growl Rate",
        juce::NormalisableRange<float> (0.15f, 0.30f, 0.001f), 0.20f, label ("x")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (IDs::growlTone, 2), "Growl Tone",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f));

    return layout;
}

} // namespace params
