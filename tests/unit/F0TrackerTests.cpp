/*
    NeoVoiDrive T27 tests — F0Tracker (MPM/NSDF), SIDEBAND_TESTPLAN §1.

    SB-U01  sine 100/200/440/1000 Hz:  steady-state error ≤ ±1 %, clarity ≥ 0.9
    SB-U02  sawtooth 110/220 Hz:       locks the fundamental, no octave error
    SB-U03  white noise:               stays unvoiced, getF0 frozen at 0
    SB-U04  voiced→unvoiced→voiced:    hold ≥ 200 ms, re-lock ≤ 150 ms, no
                                      > 12 st/hop jumps on the trajectory
    SB-U05  log glide 100→400 Hz:      tracking lag ≤ 60 ms (cross-correlation)
    SB-U06  block cadence 1/127/512/8192: results identical (±0.2 %)
    SB-U07  two tones 220+277 Hz:      degrades without losing control
                                      (≤ 12 st/hop, no NaN)
    P02     steady-state hop processing is allocation-free (g_allocCount hook)
*/

#include <juce_dsp/juce_dsp.h>

#include "dsp/F0Tracker.h"

#include <cmath>
#include <cstdint>
#include <vector>

// Global allocation audit counter (defined in FormantEngineTests.cpp).
extern std::int64_t g_allocCount;

namespace
{

constexpr double testSampleRate = 48000.0;

//==============================================================================
//  Deterministic test-signal synthesis
//==============================================================================
void fillSine (std::vector<float>& out, double freq) noexcept
{
    out.resize (static_cast<size_t> (testSampleRate * 2.0)); // 2 s
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = 0.8f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi
                                                      * freq * static_cast<double> (i) / testSampleRate));
}

void fillSaw (std::vector<float>& out, double freq) noexcept
{
    out.resize (static_cast<size_t> (testSampleRate * 2.0)); // 2 s
    double phase = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        phase += freq / testSampleRate;
        phase -= std::floor (phase);
        out[i] = 0.6f * static_cast<float> (2.0 * phase - 1.0); // rich harmonics
    }
}

void fillNoise (std::vector<float>& out, std::uint32_t seed) noexcept
{
    out.resize (static_cast<size_t> (testSampleRate * 2.0)); // 2 s
    auto rng = seed;
    for (size_t i = 0; i < out.size(); ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        out[i] = static_cast<float> (rng >> 8) / 8388608.0f - 1.0f;
    }
}

void fillGlide (std::vector<float>& out, double f0, double f1) noexcept
{
    const auto n = static_cast<size_t> (testSampleRate * 4.0); // 4 s
    out.resize (n);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / testSampleRate;
        const double f = f0 * std::pow (f1 / f0, t / 4.0); // log glide
        phase += f / testSampleRate;
        out[i] = 0.8f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * phase));
    }
}

void fillTwoTone (std::vector<float>& out, double f1, double f2) noexcept
{
    out.resize (static_cast<size_t> (testSampleRate * 2.0)); // 2 s
    double p1 = 0.0, p2 = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        p1 += f1 / testSampleRate; p1 -= std::floor (p1);
        p2 += f2 / testSampleRate; p2 -= std::floor (p2);
        out[i] = 0.3f * static_cast<float> (2.0 * p1 - 1.0)
               + 0.3f * static_cast<float> (2.0 * p2 - 1.0);
    }
}

//==============================================================================
//  Feeding helpers
//==============================================================================
void feed (dsp::F0Tracker& tracker, const std::vector<float>& sig, int blockSize)
{
    const auto n = static_cast<int> (sig.size());
    for (int i = 0; i < n; i += blockSize)
    {
        const int count = std::min (blockSize, n - i);
        for (int k = 0; k < count; ++k)
            tracker.pushSample (sig[static_cast<size_t> (i + k)]);
    }
}

// Hop-synced trajectory: f0 at every hop boundary (480 samples @ 48 kHz).
std::vector<float> hopTrajectory (dsp::F0Tracker& tracker, const std::vector<float>& sig,
                                  int blockSize, int skipHops)
{
    std::vector<float> traj;
    const auto n = static_cast<int> (sig.size());
    int sinceHop = 0;
    int hopIndex = 0;
    for (int i = 0; i < n; i += blockSize)
    {
        const int count = std::min (blockSize, n - i);
        for (int k = 0; k < count; ++k)
        {
            tracker.pushSample (sig[static_cast<size_t> (i + k)]);
            ++sinceHop;
            if (sinceHop >= 480)
            {
                sinceHop = 0;
                if (hopIndex >= skipHops)
                    traj.push_back (tracker.getF0());
                ++hopIndex;
            }
        }
    }
    return traj;
}

double steadyMean (const std::vector<float>& traj, int dropFirst)
{
    if (traj.size() <= static_cast<size_t> (dropFirst))
        return 0.0;
    double sum = 0.0;
    int count = 0;
    for (size_t i = static_cast<size_t> (dropFirst); i < traj.size(); ++i)
        if (traj[i] > 0.0f)
        {
            sum += traj[i];
            ++count;
        }
    return count > 0 ? sum / count : 0.0;
}

bool trajectoryJumpsStayBounded (const std::vector<float>& traj, float maxSemitones)
{
    for (size_t i = 1; i < traj.size(); ++i)
    {
        const float a = traj[i - 1], b = traj[i];
        if (a > 0.0f && b > 0.0f)
        {
            const float st = std::abs (1200.0f * std::log2 (b / a));
            if (st > maxSemitones)
                return false;
        }
    }
    return true;
}

//==============================================================================
//  The test suite
//==============================================================================
class F0TrackerTest final : public juce::UnitTest
{
public:
    F0TrackerTest() : juce::UnitTest ("F0Tracker") {}

    void runTest() override
    {
        runSbU01();
        runSbU02();
        runSbU03();
        runSbU04();
        runSbU04b();
        runSbU05();
        runSbU06();
        runSbU07();
        runAllocationFree();
    }

private:
    struct HopState { float f0; bool voiced; };

    void runSbU01()
    {
        beginTest ("SB-U01 sine 100/200/440/1000 Hz, error ≤ 1 %, clarity ≥ 0.9");
        {
            for (double freq : { 100.0, 200.0, 440.0, 1000.0 })
            {
                std::vector<float> sig;
                fillSine (sig, freq);

                dsp::F0Tracker tracker;
                tracker.prepare (testSampleRate, 512);
                const auto traj = hopTrajectory (tracker, sig, 1, 50); // drop 0.5 s warm-up

                const double est = steadyMean (traj, 0);
                expectWithinAbsoluteError (static_cast<float> (est), static_cast<float> (freq),
                                           static_cast<float> (freq * 0.01),
                                           juce::String (freq) + " Hz steady-state f0 error");
                expectGreaterThan (tracker.getClarity(), 0.9f,
                                   juce::String (freq) + " Hz clarity");
                expect (tracker.isVoiced(), juce::String (freq) + " Hz should be voiced");
            }
        }
    }

    void runSbU02()
    {
        beginTest ("SB-U02 sawtooth 110/220 Hz locks the fundamental, no octave error");
        {
            for (double freq : { 110.0, 220.0 })
            {
                std::vector<float> sig;
                fillSaw (sig, freq);

                dsp::F0Tracker tracker;
                tracker.prepare (testSampleRate, 512);
                const auto traj = hopTrajectory (tracker, sig, 1, 50);

                const double est = steadyMean (traj, 0);
                expectWithinAbsoluteError (static_cast<float> (est), static_cast<float> (freq),
                                           static_cast<float> (freq * 0.01),
                                           juce::String (freq) + " Hz sawtooth f0 (octave error?)");
                // Explicit octave-error guard: estimate must NOT sit at 2× or 0.5×.
                expect (std::abs (est / freq - 2.0) > 0.05 && std::abs (est / freq - 0.5) > 0.05,
                        juce::String (freq) + " Hz locked on an octave (2× or 0.5×)");
            }
        }
    }

    void runSbU03()
    {
        beginTest ("SB-U03 white noise stays unvoiced, getF0 frozen at 0");
        {
            std::vector<float> sig;
            fillNoise (sig, 12345u);

            dsp::F0Tracker tracker;
            tracker.prepare (testSampleRate, 512);

            bool allUnvoiced = true;
            bool allZero = true;
            int sinceHop = 0;
            for (float x : sig)
            {
                tracker.pushSample (x);
                ++sinceHop;
                if (sinceHop >= 480)
                {
                    sinceHop = 0;
                    allUnvoiced = allUnvoiced && (! tracker.isVoiced());
                    allZero = allZero && (tracker.getF0() == 0.0f);
                }
            }
            expect (allUnvoiced, "noise input must stay unvoiced");
            expect (allZero, "getF0 must stay frozen at 0 for noise");
            expectLessThan (tracker.getClarity(), 0.45f, "noise clarity < 0.45");
        }
    }

    void runSbU04()
    {
        beginTest ("SB-U04 voiced→unvoiced→voiced: hold ≥ 200 ms, re-lock ≤ 150 ms");
        {
            std::vector<float> first, gap, second;
            fillSaw (first, 220.0);
            fillSaw (second, 330.0);
            gap.assign (static_cast<size_t> (testSampleRate * 1.0), 0.0f); // 1.0 s silence (T36 rev24: hold is 600 ms)

            dsp::F0Tracker tracker;
            tracker.prepare (testSampleRate, 512);

            // Track state per hop across the whole sequence.
            std::vector<HopState> trace;
            const auto feedAndTrace = [&] (const std::vector<float>& sig, int& hopCounter)
            {
                int sinceHop = 0;
                for (float x : sig)
                {
                    tracker.pushSample (x);
                    ++sinceHop;
                    if (sinceHop >= 480)
                    {
                        sinceHop = 0;
                        trace.push_back ({ tracker.getF0(), tracker.isVoiced() });
                        ++hopCounter;
                    }
                }
            };

            int hop = 0;
            const int voicedHops = static_cast<int> (first.size() / 480);
            const int gapHops    = static_cast<int> (gap.size() / 480);
            feedAndTrace (first, hop);
            feedAndTrace (gap, hop);

            // Hold: after silence begins, isVoiced must persist ≥ 200 ms.
            const int holdMinHops = static_cast<int> (0.200 * testSampleRate / 480.0); // 20 hops
            int heldHops = 0;
            for (int i = voicedHops; i < voicedHops + gapHops; ++i)
                if (trace[static_cast<size_t> (i)].voiced)
                    ++heldHops;
            expectGreaterOrEqual (heldHops, holdMinHops,
                                  juce::String ("hold only lasted ") + juce::String (heldHops)
                                      + " hops (< 200 ms)");

            // After the hold times out, the tracker must report unvoiced.
            expect (! trace[static_cast<size_t> (voicedHops + gapHops - 1)].voiced,
                    "should be unvoiced at the end of the gap");

            // Re-lock: after the 330 Hz tone starts, lock within 150 ms.
            const int relockMaxHops = static_cast<int> (0.150 * testSampleRate / 480.0); // 15 hops
            const int gapEnd = voicedHops + gapHops;
            feedAndTrace (second, hop);
            int lockedHop = -1;
            for (int i = gapEnd; i < static_cast<int> (trace.size()); ++i)
            {
                const float f = trace[static_cast<size_t> (i)].f0;
                if (f > 0.0f && std::abs (f - 330.0f) / 330.0f < 0.01f)
                {
                    lockedHop = i;
                    break;
                }
            }
            expect (lockedHop >= 0, "never re-locked 330 Hz");
            if (lockedHop >= 0)
                expectLessOrEqual (lockedHop - gapEnd, relockMaxHops,
                                   juce::String ("re-lock took ") + juce::String (lockedHop - gapEnd)
                                       + " hops (> 150 ms)");

            // Slope limit: no > 12 st jump between consecutive non-zero f0s.
            expect (trajectoryJumpsStayBounded (traceToF0 (trace), 12.0f + 0.5f),
                    "trajectory jumped more than 12 st/hop");
        }
    }

    void runSbU04b()
    {
        beginTest ("SB-U04b hold times out with clarity stuck in the hysteresis band (P2-4)");
        {
            std::vector<float> sig;
            fillSaw (sig, 220.0);

            dsp::F0Tracker tracker;
            tracker.prepare (testSampleRate, 512);
            tracker.setActive (true);
            for (float x : sig)
                tracker.pushSample (x);

            expect (tracker.getState() == dsp::F0Tracker::State::Voiced,
                    "tracker is not voiced after the 220 Hz input");
            const auto frozenF0 = tracker.getF0();
            expectGreaterThan (frozenF0, 0.0f);

            // Clarity 0.50 sits inside [0.45, 0.60): it must not re-lock,
            // but the hold timer starts and must expire at 600 ms.
            tracker.updateStateForTest (0.50f);
            expect (tracker.getState() == dsp::F0Tracker::State::Hold,
                    "a sub-threshold frame must enter Hold");
            expect (tracker.isVoiced(), "Hold must still report voiced");
            expectWithinAbsoluteError (tracker.getF0(), frozenF0, 0.01f,
                                       "Hold must freeze f0");

            for (int hop = 0; hop < 58; ++hop) // 590 ms total
                tracker.updateStateForTest (0.50f);

            expect (tracker.getState() == dsp::F0Tracker::State::Hold,
                    "hold expired before 600 ms");
            expect (tracker.isVoiced(), "hold expired before 600 ms");

            tracker.updateStateForTest (0.50f); // 600 ms
            tracker.updateStateForTest (0.50f); // guard against float edge rounding
            expect (tracker.getState() == dsp::F0Tracker::State::Unvoiced,
                    "hold did not time out in the hysteresis band");
            expect (! tracker.isVoiced(), "timed-out hold still reports voiced");
            expectEquals (tracker.getF0(), 0.0f);

            // A confident frame re-locks immediately.
            tracker.updateStateForTest (0.70f, 330.0f);
            expect (tracker.getState() == dsp::F0Tracker::State::Voiced,
                    "did not re-lock after the hold timeout");
            expectWithinAbsoluteError (tracker.getF0(), 330.0f, 1.0f,
                                       "re-lock did not take the new f0");
        }
    }

    void runSbU05()
    {
        beginTest ("SB-U05 log glide 100→400 Hz, tracking lag ≤ 60 ms");
        {
            std::vector<float> sig;
            fillGlide (sig, 100.0, 400.0);

            dsp::F0Tracker tracker;
            tracker.prepare (testSampleRate, 512);
            const auto traj = hopTrajectory (tracker, sig, 1, 0);

            // Trim the leading unvoiced hops (f0 == 0 during window fill) from
            // BOTH sequences.
            size_t start = 0;
            while (start < traj.size() && traj[start] <= 0.0f)
                ++start;

            // Reference trajectory sampled at the same hop instants.
            std::vector<double> ref;
            for (size_t i = start; i < traj.size(); ++i)
            {
                const double t = static_cast<double> (i) * 480.0 / testSampleRate;
                ref.push_back (100.0 * std::pow (4.0, t / 4.0));
            }

            // Lag estimate: weighted least-squares time shift. (Plain
            // cross-correlation is unusable here: a smooth monotonic glide
            // correlates ~1.0 with ANY shift, so the peak pins to the search
            // boundary. LS on the local slope recovers the true shift.)
            double num = 0.0, den = 0.0;
            for (size_t i = 1; i + 1 < ref.size(); ++i)
            {
                const double slope = (ref[i + 1] - ref[i - 1]) * 0.5; // Hz per hop
                const double err   = ref[i] - static_cast<double> (traj[start + i]);
                num += err * slope;
                den += slope * slope;
            }
            const double lagHops = den > 1e-12 ? num / den : 0.0;
            const double lagMs = std::abs (lagHops) * 480.0 * 1000.0 / testSampleRate;
            expectLessOrEqual (lagMs, 60.0,
                               juce::String ("tracking lag ") + juce::String (lagMs) + " ms > 60 ms");
        }
    }

    void runSbU06()
    {
        beginTest ("SB-U06 block cadence 1/127/512/8192 is equivalent (±0.2 %)");
        {
            std::vector<float> sig;
            fillSine (sig, 440.0);

            double reference = 0.0;
            for (int block : { 1, 127, 512, 8192 })
            {
                dsp::F0Tracker tracker;
                tracker.prepare (testSampleRate, 512);
                const auto traj = hopTrajectory (tracker, sig, block, 50);
                const double est = steadyMean (traj, 0);
                if (block == 1)
                    reference = est;
                expectWithinAbsoluteError (static_cast<float> (est), static_cast<float> (reference),
                                           static_cast<float> (reference * 0.002),
                                           juce::String ("block ") + juce::String (block)
                                               + " differs from block 1");
            }
        }
    }

    void runSbU07()
    {
        beginTest ("SB-U07 two tones 220+277 Hz degrade gracefully (≤ 12 st/hop, no NaN)");
        {
            std::vector<float> sig;
            fillTwoTone (sig, 220.0, 277.0);

            dsp::F0Tracker tracker;
            tracker.prepare (testSampleRate, 512);
            const auto traj = hopTrajectory (tracker, sig, 1, 0);

            bool allFinite = true;
            for (float f : traj)
                allFinite = allFinite && std::isfinite (f);
            expect (allFinite, "polyphonic input produced NaN f0");

            expect (trajectoryJumpsStayBounded (traj, 12.0f + 0.5f),
                    "polyphonic f0 jumped more than 12 st/hop (runaway)");

            // Degradation must be visible: clarity drops well below the clean
            // sine case (≥ 0.9) — the tracker is not confidently voiced.
            expectLessThan (tracker.getClarity(), 0.9f, "polyphonic clarity should drop");
        }
    }

    void runAllocationFree()
    {
        beginTest ("steady-state hop processing is allocation-free (P02 hook)");
        {
            dsp::F0Tracker tracker;
            tracker.prepare (testSampleRate, 512);

            // Warm-up: FFT tables and caches are built in prepare(); a few
            // hops flush any lazy init before we start counting.
            std::vector<float> sig;
            fillSine (sig, 220.0);
            feed (tracker, sig, 512);

            // Build the failure message BEFORE sampling: expectEquals() takes
            // a juce::String, whose construction from a literal allocates —
            // building it inside the sampled window would add +1.
            const juce::String failMessage = "hop processing allocated on the steady-state path";
            const auto before = g_allocCount;
            feed (tracker, sig, 512); // ~200 more hops
            expectEquals (static_cast<int> (g_allocCount - before), 0, failMessage);
        }
    }

    static std::vector<float> traceToF0 (const std::vector<HopState>& trace)
    {
        std::vector<float> f;
        f.reserve (trace.size());
        for (const auto& h : trace)
            f.push_back (h.f0);
        return f;
    }
};

F0TrackerTest f0TrackerTest;

} // namespace
