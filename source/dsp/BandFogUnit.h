#pragma once

/*
    BandFogUnit — whole-band Rough (T49; shared FogCore T51, constant-envelope
    phase fog T58/T59/T60. SPEC §4.3 / DECISIONS R44/R45/R50). User-facing
    name: "Rough" in Band Effects (T50).

    The Sideband module's Rough knob only hazes its WET path. This unit is an
    independent, engine-agnostic fog stage on the WHOLE band output: the
    BandProcessor applies it after the Legacy/Sideband mix and before the band
    gain, so a band can be fogged even with Sideband Depth = 0 or with the
    Legacy engine.

    Topology (one instance per channel):

        in ──► FogCore::PhaseFog ──► out

    The phase fog rotates the analytic signal (8-section IIR Hilbert pair) by
    a band-limited random phase. The rotation is unitary, so it does not add
    amplitude modulation, does not add a noise layer, and keeps the total
    energy. The control RNGs only drive phase; all audio is the input's own
    harmonic content. See FogCore.h for the fineC/T59/T60 constants and
    measured spectrum (1 kHz R50: 32.0 % inside ±5 Hz, 69.8 % inside ±100 Hz).

    Rough = 0 → exact identity; the phase fog is reset while off.
    There is no delay line: this unit reports 0 latency (R21/R44) and does not
    change PDC. The Sideband Lmax deliberately keeps its historical F reserve
    (T45 compatibility) even though T58/T59/T60 no longer use a fog delay read.

    RT contract: zero allocation, no locks, no exceptions, no IO.
*/

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdint>

#include "FogCore.h"
#include "../util/SmoothedValue.h"

namespace dsp
{

class BandFogUnit
{
public:
    struct Params
    {
        float amount = 0.0f;   // % (0 = transparent, 100 = full fog)
    };

    static constexpr float smoothingMilliseconds = 20.0f; // SPEC §4.3

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        phaseFog.prepare (sampleRate);

        amountSmooth.prepare (sampleRate, smoothingMilliseconds);
        amountSmooth.reset (0.0f);

        reset();
    }

    /** Once per block (audio thread). NaN-safe like every other DSP unit. */
    void setParameters (const Params& p) noexcept
    {
        const auto finiteAmount = std::isfinite (p.amount) ? p.amount : 0.0f;
        amountSmooth.setTarget (juce::jlimit (0.0f, 100.0f, finiteAmount));
    }

    void setSeed (std::int64_t seed) noexcept
    {
        phaseFog.setSeed (seed ^ 0x0F060F060F060F06LL);
    }

    /** Clears the Hilbert/phase-control state. Seeds and the smoothed amount
        are kept. */
    void reset() noexcept
    {
        phaseFog.reset();
        lastAmount = 0.0f;
    }

    /** One sample in, one sample out (audio thread). */
    float process (float x) noexcept
    {
        const float amount = amountSmooth.getNext() * 0.01f; // 0..1
        lastAmount = amount;

        if (amount <= 1.0e-6f)
        {
            phaseFog.reset();
            return x;
        }

        return phaseFog.process (x, amount);
    }

    //==========================================================================
    // Diagnostics (tests)
    float getFogAmount() const noexcept { return lastAmount; }
    float getSmoothedAmount() const noexcept { return amountSmooth.getCurrentValue() * 0.01f; }
    float getLastPhaseRadians() const noexcept { return phaseFog.getLastPhase(); }

private:
    FogCore::PhaseFog phaseFog;
    util::SmoothedValue amountSmooth;

    double sampleRate = 48000.0;
    float lastAmount = 0.0f;
};

} // namespace dsp
