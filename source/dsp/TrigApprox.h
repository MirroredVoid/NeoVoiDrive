#pragma once

/*
    TrigApprox — the sin/cos/tanh implementation used by FogCore,
    SidebandEngine and GrowlEngine.

    TRIG-AB (2026-11-05) measured the JUCE Padé approximants against std:: on
    the complete chain: normal material differs by −124…−128 dBFS RMS, the
    worst case (all effects at 100 %, 0.9 FS input) by −104 dBFS RMS / −81 dBFS
    peak, i.e. below 16-bit dither noise and inaudible. The approximants need
    their documented input ranges, so:

      - sin/cos: phase reduced to [-pi, pi];
      - tanh: input clamped to [-5, 5] (|tanh| is already 1 there).

    Keep this file as the single home for the approximation so the three call
    sites cannot drift apart.
*/

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace dsp::trig
{

inline float wrapToPi (float x) noexcept
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr float pi = juce::MathConstants<float>::pi;
    const float k = x * (1.0f / twoPi);
    const float r = x - twoPi * std::floor (k + 0.5f);
    return r > pi ? r - twoPi : r;
}

inline float sin (float x) noexcept
{
    return juce::dsp::FastMathApproximations::sin (wrapToPi (x));
}

inline float cos (float x) noexcept
{
    return juce::dsp::FastMathApproximations::cos (wrapToPi (x));
}

inline float tanh (float x) noexcept
{
    return juce::dsp::FastMathApproximations::tanh (juce::jlimit (-5.0f, 5.0f, x));
}

} // namespace dsp::trig
