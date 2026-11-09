#pragma once

/*
    Three-band Linkwitz-Riley crossover bank (T10, DECISIONS §2.1 / A5).

        lo  = LP(f1) ^ stages
        mid = HP(f1) ^ stages -> LP(f2) ^ stages
        hi  = HP(f2) ^ stages

    Slopes (A5): 12 dB/oct = LR2 (2x 1st-order Butterworth), 24 dB/oct = LR4
    (2x 2nd-order, Q=0.7071), 36 dB/oct = LR6, 48 dB/oct = LR8 (2x 4th-order;
    each 4th-order is two 2nd-order sections Q=0.5412 / 1.3066), 60 dB/oct =
    LR10, 72 dB/oct = LR12.

    All-pass sum property: for LR4/LR8 the bands are phase-aligned at the
    crossover points, so lo + mid + hi has unity magnitude over the whole band.
    LR2 has a 180 deg phase difference between low- and high-pass at the
    crossover (magnitude cancellation); the mid band is therefore polarity
    INVERTED for the 12 dB/oct setting, which restores the flat sum at both
    crossover points (mathematically verified in the unit tests).

    Coefficients are recomputed per block and ramped per sample (20 ms linear
    ramp) — no coefficient steps, no clicks.  A SLOPE change also flips the
    mid polarity and the active stage count, which a coefficient ramp cannot
    smooth; those changes crossfade the previous bank's output into the new
    bank over the same 20 ms (audit P1-3), so no sample-level polarity step
    reaches the output.
    SPEC §4.4: f1 in [40, 18000]; effective f2 = max (f2, f1 * 1.05).

    Zero latency (IIR, DECISIONS §3.1). RT contract: zero allocation, no
    locks, no exceptions.
*/

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace dsp
{

class CrossoverBank
{
public:
    enum class Slope
    {
        slope12, // 12 dB/oct, LR2
        slope24, // 24 dB/oct, LR4
        slope36, // 36 dB/oct, LR6
        slope48, // 48 dB/oct, LR8
        slope60, // 60 dB/oct, LR10
        slope72  // 72 dB/oct, LR12
    };

    // Per-path section capacity: 6 stages (LR10/LR12) x 4 paths.
    static constexpr int maxStages = 6;

    // SPEC §4.4 clamping bounds.
    static constexpr float minFreq = 40.0f;
    static constexpr float maxFreq = 18000.0f;
    static constexpr float minFreqRatio = 1.05f; // effective f2 >= f1 * 1.05

    // SPEC §4.2 smoothing column (xover frequencies).
    static constexpr float smoothingMilliseconds = 20.0f;

    /** Configures the sample rate and (re)initialises the bank. */
    void prepare (double newSampleRate, int /*maxBlockSize*/) noexcept
    {
        sampleRate = newSampleRate;
        rampSteps = juce::jmax (1, static_cast<int> (smoothingMilliseconds * 0.001 * sampleRate));

        for (auto& section : filters)
            section = Section{}; // direct (unity) coefficients

        // Initial configuration: SPEC defaults, applied instantly.
        setCrossover (200.0f, 4000.0f, Slope::slope24);
        for (auto& section : filters)
            section.snapToTarget();

        // No ramp pending: the coefficients already equal the targets.
        for (auto& section : filters)
            section.countdown = 0;
        crossfadeRemaining = 0;
        lastF1 = 200.0f;
        lastF2 = 4000.0f;
        lastSlope = Slope::slope24;
    }

    /** Sets crossover frequencies and slope (once per block). RT safe.

        Only a real parameter change restarts the coefficient ramp; calling
        this with unchanged values every block (as the processor does) leaves
        the ongoing ramp untouched.
    */
    void setCrossover (float f1, float f2, Slope newSlope) noexcept
    {
        // SPEC §4.4: DSP-layer clamping (user values stay untouched).
        // Note: at the f1 upper bound (18000) the 1.05 ratio cannot be
        // honoured without exceeding maxFreq — maxFreq wins (SPEC conflict).
        f1 = juce::jlimit (minFreq, maxFreq, f1);
        const auto f2Effective = juce::jlimit (minFreq, maxFreq,
                                               juce::jmax (f2, f1 * minFreqRatio));

        const bool unchanged = (f1 == lastF1 && f2Effective == lastF2 && newSlope == lastSlope);
        const bool slopeChanged = (newSlope != lastSlope);
        lastF1 = f1;
        lastF2 = f2Effective;
        lastSlope = newSlope;

        if (unchanged)
            return;

        if (slopeChanged)
        {
            // Audit P1-3: the slope change flips the mid polarity and the
            // active stage count, which a coefficient ramp cannot smooth.
            // Keep the current bank processing the input and crossfade its
            // output into the new configuration over the same 20 ms. The
            // old bank's coefficients/state are copied here (frozen); the new
            // bank snaps straight to its targets.
            oldFilters = filters;
            oldActiveStages = activeStages;
            oldMidPolarity = midPolarity;
            for (auto& section : oldFilters)
                section.countdown = 0;
            crossfadeSteps = rampSteps;
            crossfadeRemaining = rampSteps;
        }

        currentSlope = newSlope;
        // Per-path section count: LRn = (Butterworth n/2)^2, each Butterworth
        // half is 1st-order sections (n/2 = 3,5) or 2nd-order sections.
        activeStages = (newSlope == Slope::slope12 || newSlope == Slope::slope24) ? 2
                     : (newSlope == Slope::slope36 || newSlope == Slope::slope48) ? 4
                                                                                   : 6;
        // LRn = (Butterworth n/2)²: the LP/HP phase difference is 180°·(n/2).
        // Odd n/2 (LR2/6/10: n/2 = 1/3/5) puts lo and mid in antiphase at the
        // crossover — the mid band needs polarity inversion (as LR2 already
        // did); even n/2 (LR4/8/12) is phase-aligned.
        midPolarity  = ((newSlope == Slope::slope12)
                     || (newSlope == Slope::slope36)
                     || (newSlope == Slope::slope60)) ? -1.0f : 1.0f;

        // lo:  LP(f1)      -> sections [0..5]
        // mid: HP(f1)      -> sections [6..11], then LP(f2) -> [12..17]
        // hi:  HP(f2)      -> sections [18..23]
        for (int i = 0; i < activeStages; ++i)
        {
            setSection (filters[i],      f1,          false, i);
            setSection (filters[i + 6],  f1,          true,  i);
            setSection (filters[i + 12], f2Effective, false, i);
            setSection (filters[i + 18], f2Effective, true,  i);
        }

        for (int i = activeStages; i < 6; ++i)
        {
            filters[i].setDirect();
            filters[i + 6].setDirect();
            filters[i + 12].setDirect();
            filters[i + 18].setDirect();
        }

        if (slopeChanged)
        {
            // The crossfade carries the transition; snapping avoids a second
            // (coefficient) ramp under it.
            for (auto& section : filters)
                section.snapToTarget();
        }
        else
        {
            // (Re)start the linear coefficient ramp for all sections.
            for (auto& section : filters)
                section.beginRamp (rampSteps);
        }
    }

    /** Clears the filter states and cancels a pending slope crossfade
        (structural routing changes; the configuration is preserved). */
    void resetState() noexcept
    {
        for (auto& section : filters)
        {
            section.x1 = section.x2 = section.y1 = section.y2 = 0.0;
            section.countdown = 0;
            section.snapToTarget();
        }
        crossfadeRemaining = 0;
    }

    /** Splits one input sample into the three bands (per channel instance). */
    void process (float x, float& lo, float& mid, float& hi) noexcept
    {
        // Per-sample linear coefficient ramp (20 ms).
        for (auto& section : filters)
        {
            if (section.countdown > 0)
                section.advance();
        }

        if (crossfadeRemaining > 0)
        {
            // Old bank: frozen coefficients/state, same input; new bank: the
            // snapped new configuration. Linear 20 ms crossfade, old weight
            // starts at 1 so the first sample is continuous with the output
            // before the change.
            const auto oldLo  = runChain (oldFilters, 0, x, oldActiveStages);
            const auto oldMidInner = runChain (oldFilters, 6, x, oldActiveStages);
            const auto oldMid = oldMidPolarity * runChain (oldFilters, 12, oldMidInner,
                                                           oldActiveStages);
            const auto oldHi  = runChain (oldFilters, 18, x, oldActiveStages);

            const auto newLo  = runChain (filters, 0, x, activeStages);
            const auto newMidInner = runChain (filters, 6, x, activeStages);
            const auto newMid = midPolarity * runChain (filters, 12, newMidInner, activeStages);
            const auto newHi  = runChain (filters, 18, x, activeStages);

            const auto oldWeight = static_cast<float> (crossfadeRemaining)
                                 / static_cast<float> (juce::jmax (1, crossfadeSteps));
            const auto newWeight = 1.0f - oldWeight;

            lo  = oldLo  * oldWeight + newLo  * newWeight;
            mid = oldMid * oldWeight + newMid * newWeight;
            hi  = oldHi  * oldWeight + newHi  * newWeight;

            --crossfadeRemaining;
            return;
        }

        lo  = runChain (filters, 0, x, activeStages);
        auto m = runChain (filters, 6, x, activeStages);
        mid = midPolarity * runChain (filters, 12, m, activeStages);
        hi  = runChain (filters, 18, x, activeStages);
    }

    //==========================================================================
private:
    //==========================================================================
    struct Section
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; // current
        double t0 = 1.0, t1 = 0.0, t2 = 0.0, u1 = 0.0, u2 = 0.0; // target
        double d0 = 0.0, d1 = 0.0, d2 = 0.0, e1 = 0.0, e2 = 0.0; // per-sample increments
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;            // state
        int countdown = 0;

        void setDirect() noexcept
        {
            t0 = 1.0; t1 = t2 = u1 = u2 = 0.0;
        }

        void setTarget (double nb0, double nb1, double nb2, double na1, double na2) noexcept
        {
            t0 = nb0; t1 = nb1; t2 = nb2; u1 = na1; u2 = na2;
        }

        void snapToTarget() noexcept
        {
            b0 = t0; b1 = t1; b2 = t2; a1 = u1; a2 = u2;
        }

        // Starts a linear coefficient ramp: `steps` samples of fixed increments,
        // landing exactly on the target (no residual step at the end).
        void beginRamp (int steps) noexcept
        {
            const auto inv = 1.0 / static_cast<double> (steps);
            d0 = (t0 - b0) * inv;
            d1 = (t1 - b1) * inv;
            d2 = (t2 - b2) * inv;
            e1 = (u1 - a1) * inv;
            e2 = (u2 - a2) * inv;
            countdown = steps;
        }

        void advance() noexcept
        {
            --countdown;
            if (countdown == 0)
            {
                snapToTarget(); // linear ramp ends exactly on the target
            }
            else
            {
                b0 += d0;
                b1 += d1;
                b2 += d2;
                a1 += e1;
                a2 += e2;
            }
        }

        float run (float x) noexcept
        {
            const auto y = b0 * static_cast<double> (x) + b1 * x1 + b2 * x2
                         - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return static_cast<float> (y);
        }
    };

    //==========================================================================
    float runChain (std::array<Section, maxStages * 4>& sections, int offset, float x,
                    int stages) noexcept
    {
        for (int i = 0; i < stages; ++i)
            x = sections[offset + i].run (x);
        return x;
    }

    //==========================================================================
    // Butterworth section coefficients (RBJ). LRn = (Butterworth n/2)^2; each
    // Butterworth half decomposes into 1st-order sections (odd order) and/or
    // 2nd-order sections whose Q = 1 / (2·cos θ) for pole angles θ. The LR6
    // (n=3: Q=1.0), LR10 (n=5: Q=0.5257, 0.8507) and LR12 (n=6: Q=0.5176,
    // 0.7071, 1.9319) pairs repeat once for the LR squaring.
    //==========================================================================
    struct StageSpec
    {
        bool  firstOrder;
        float q;
    };

    static const StageSpec* stageSpecs (Slope slope) noexcept
    {
        static constexpr StageSpec s12[] = { { true, 0.0f },        { true, 0.0f } };
        static constexpr StageSpec s24[] = { { false, 0.7071f },    { false, 0.7071f } };
        static constexpr StageSpec s36[] = { { true, 0.0f },        { false, 1.0f },
                                              { true, 0.0f },        { false, 1.0f } };
        static constexpr StageSpec s48[] = { { false, 0.5412f },    { false, 1.3066f },
                                              { false, 0.5412f },    { false, 1.3066f } };
        static constexpr StageSpec s60[] = { { true, 0.0f },        { false, 1.6180f },
                                              { false, 0.6180f },    { true, 0.0f },
                                              { false, 1.6180f },    { false, 0.6180f } };
        static constexpr StageSpec s72[] = { { false, 0.5176f },    { false, 0.7071f },
                                              { false, 1.9319f },    { false, 0.5176f },
                                              { false, 0.7071f },    { false, 1.9319f } };

        switch (slope)
        {
            case Slope::slope12: return s12;
            case Slope::slope24: return s24;
            case Slope::slope36: return s36;
            case Slope::slope60: return s60;
            case Slope::slope72: return s72;
            case Slope::slope48: [[fallthrough]];
            default:             return s48;
        }
    }

    void setSection (Section& section, float freq, bool highpass, int stageIndex) noexcept
    {
        const auto* spec = stageSpecs (currentSlope);

        auto omega = juce::MathConstants<double>::twoPi * static_cast<double> (freq) / sampleRate;
        if (omega > 2.8) // ~= 0.9 * pi: keep the biquads stable at low rates
            omega = 2.8;

        const auto cosW = std::cos (omega);
        const auto sinW = std::sin (omega);

        if (spec[stageIndex].firstOrder)
        {
            // 1st-order Butterworth (LR2, and the odd-order halves of LR6/LR10).
            const auto k = std::tan (omega * 0.5);
            const auto inv = 1.0 / (1.0 + k);

            if (highpass)
                section.setTarget (inv, -inv, 0.0, (k - 1.0) * inv, 0.0);
            else
                section.setTarget (k * inv, k * inv, 0.0, (k - 1.0) * inv, 0.0);
            return;
        }

        // 2nd-order Butterworth section; Q depends on the slope's stage spec.
        const auto q = static_cast<double> (spec[stageIndex].q);
        const auto alpha = sinW / (2.0 * q);
        const auto a0 = 1.0 + alpha;

        double b0, b1, b2;
        if (highpass)
        {
            b0 = (1.0 + cosW) * 0.5;
            b1 = -(1.0 + cosW);
            b2 = (1.0 + cosW) * 0.5;
        }
        else
        {
            b0 = (1.0 - cosW) * 0.5;
            b1 = 1.0 - cosW;
            b2 = (1.0 - cosW) * 0.5;
        }

        section.setTarget (b0 / a0, b1 / a0, b2 / a0, (-2.0 * cosW) / a0, (1.0 - alpha) / a0);
    }

    //==========================================================================
    double sampleRate = 48000.0;
    int rampSteps = 960;
    int activeStages = 2;
    Slope currentSlope = Slope::slope24;
    float midPolarity = 1.0f;
    float lastF1 = 0.0f;
    float lastF2 = 0.0f;
    Slope lastSlope = Slope::slope24;
    std::array<Section, maxStages * 4> filters {};

    // Audit P1-3: old configuration used for the slope-change crossfade.
    std::array<Section, maxStages * 4> oldFilters {};
    int oldActiveStages = 2;
    float oldMidPolarity = 1.0f;
    int crossfadeSteps = 0;
    int crossfadeRemaining = 0;
};

} // namespace dsp
