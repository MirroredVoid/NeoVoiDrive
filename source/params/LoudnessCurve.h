#pragma once

/*
    T71: loudness knob control law (band gain, SPEC §4.3).

    Normalised knob position x -> output dB:
      - x = 0.5  ->  0 dB
      - x = 1    -> +12 dB
      - x -> 0+  -> -48 dB (the last reachable dB value)
      - x = 0    -> exact mute (-inf); the parameter stores the -48 dB
                    sentinel and the DSP maps it to internalMuteDb.

    Attenuation uses 0.08|d| + 0.92|d|^3 (slow near the default, strongly
    accelerating); boost uses 0.80 d + 0.20 d^3 (near-linear, slight
    acceleration), where d = (x - 0.5)/0.5.

    The curve is the single source for the parameter range, the knob text and
    the DSP mute gate. It is header-only and allocation-free.
*/

#include <juce_core/juce_core.h>

namespace params::loudness
{

inline constexpr float minDb = -48.0f;
inline constexpr float maxDb = 12.0f;

// Internal target for the exact-mute sentinel. The band-gain smoother runs in
// the dB domain, so it fades towards this value; once the smoothed dB crosses
// muteThresholdDb the DSP applies exact zero (no residual noise).
inline constexpr float internalMuteDb = -100.0f;
inline constexpr float muteThresholdDb = -99.0f;

inline float shapeDown (float d) noexcept
{
    return 0.08f * d + 0.92f * d * d * d;
}

inline float shapeUp (float d) noexcept
{
    return 0.80f * d + 0.20f * d * d * d;
}

/** Inverse of a*d + b*d^3 for d/y in [0, 1] (Newton; a+b = 1). */
inline float inverseShape (float y, float a, float b) noexcept
{
    y = juce::jlimit (0.0f, 1.0f, y);
    float d = y;
    for (int i = 0; i < 6; ++i)
    {
        const float f  = a * d + b * d * d * d - y;
        const float df = a + 3.0f * b * d * d;
        if (df > 1.0e-9f)
            d = juce::jlimit (0.0f, 1.0f, d - f / df);
    }
    return d;
}

/** Normalised knob position -> dB. x == 0 returns the -48 dB mute sentinel. */
inline float dbFromNorm (float norm) noexcept
{
    norm = juce::jlimit (0.0f, 1.0f, norm);
    if (norm <= 0.0f)
        return minDb;
    if (norm >= 1.0f)
        return maxDb;

    const float d = (norm - 0.5f) * 2.0f;
    if (d > 0.0f)
        return maxDb * shapeUp (d);
    if (d < 0.0f)
        return minDb * shapeDown (-d);
    return 0.0f;
}

/** dB -> normalised knob position (0 for the -48 dB mute sentinel). */
inline float normFromDb (float db) noexcept
{
    if (db <= minDb)
        return 0.0f;
    if (db >= maxDb)
        return 1.0f;
    if (db >= 0.0f)
        return 0.5f + 0.5f * inverseShape (db / maxDb, 0.80f, 0.20f);

    return 0.5f - 0.5f * inverseShape (-db / -minDb, 0.08f, 0.92f);
}

/** -48 dB is the stored mute sentinel (the UI displays -inf). */
inline bool isMuteDb (float db) noexcept
{
    return db <= minDb + 1.0e-4f;
}

/** dB target handed to the band-gain smoother; mute fades to internalMuteDb. */
inline float dspTargetDb (float db) noexcept
{
    return isMuteDb (db) ? internalMuteDb : db;
}

/** UI text for the mute state (the slider appends "dB"). */
inline juce::String muteText()
{
    return juce::String::charToString (0x2212) + juce::String::charToString (0x221e);
}

inline juce::NormalisableRange<float> makeRange() noexcept
{
    return juce::NormalisableRange<float> (
        minDb, maxDb,
        [] (float, float, float norm) { return dbFromNorm (norm); },
        [] (float, float, float db)   { return normFromDb (db); },
        [] (float, float, float value) { return value; });
}

} // namespace params::loudness
