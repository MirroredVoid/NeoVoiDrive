#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>

#include "PluginProcessor.h"
#include "dsp/HarmonicExciter.h"
#include "dsp/SidebandEngine.h"   // T45: maxLatencySamplesForRate (fixed Lmax)

namespace
{

// T22: latency verification — impulse method (TESTPLAN §3, L01..L04).
//
// Drives the PRODUCTION PluginProcessor directly (NOT the T21 RenderCore,
// which already compensates the formant latency): an impulse at sample 0 is
// rendered block by block, the output peak position is the MEASURED latency,
// and it is compared against the REPORTED latency
// (AudioProcessor::getLatencySamples — what the host sees).
//
// Acceptance (T22): off = 0 ± 8 samples, on = 2048 ± 8 samples, retested at
// 44.1k / 96k / 192k; block size 1..8192 must not change the result.
// The exciter's 2x oversampling group delay (< 10 samples, non-integer,
// reported as 0 — SPEC §5.1 / R11) is measured and logged for the record.
class LatencyMeasureTests final : public juce::UnitTest
{
public:
    LatencyMeasureTests() : juce::UnitTest ("LatencyMeasure") {}

    struct Result
    {
        int peakIndex = -1;   // measured latency (samples)
        float peak = 0.0f;
        int significant = 0;  // samples above 0.2 (single-impulse check)
        int reported = -1;    // getLatencySamples() after rendering
        bool finite = true;
    };

    /** Renders a 1-sample impulse and locates the output peak.
        With formant ON the engine first holds its gain for 2048 samples and
        the dry path passes straight through (S1 warm-up), so we warm up
        `warmupSeconds` of silence BEFORE the impulse — the steady-state
        latency is what the report describes. */
    static Result runImpulse (int sampleRate, int blockSize, bool formantOn,
                              double warmupSeconds = 0.5)
    {
        PluginProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);

        if (auto* param = processor.apvts.getParameter (params::IDs::formantEnabled))
            param->setValueNotifyingHost (formantOn ? 1.0f : 0.0f);
        // T35: runImpulse is the LEGACY golden path — pin engine=0 (the
        // default flipped to Sideband at Phase 6; SB-L01 covers the sideband
        // impulse latency explicitly).
        if (auto* engine = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter (params::IDs::engineVersion)))
            *engine = 0;

        const auto total = static_cast<int> (sampleRate * (warmupSeconds + 1.0)); // 1 s after warmup
        const auto impulsePos = static_cast<int> (sampleRate * warmupSeconds);
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;

        Result r;
        int pos = 0;
        while (pos < total)
        {
            buf.clear();
            if (impulsePos >= pos && impulsePos < pos + blockSize)
                buf.setSample (0, impulsePos - pos, 1.0f); // impulse (after warmup)

            processor.processBlock (buf, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = buf.getSample (0, i);
                r.finite = r.finite && std::isfinite (v);
                const auto a = std::abs (v);
                if (a > 0.2f)
                    ++r.significant;
                if (a > r.peak)
                {
                    r.peak = a;
                    r.peakIndex = pos + i - impulsePos; // relative to the impulse
                }
            }
            pos += blockSize;
        }

        r.reported = processor.getLatencySamples();
        return r;
    }

    /** T31/T45: impulse latency with the SIDEBAND engine engaged
        (SIDEBAND_SPEC §9.1). The wet path is unvoiced for an impulse (the F0
        tracker stays silent), so the measured peak is the fixed dry baseline
        Lmax = A(100 %) + F + 4 — the same value reported to the host. */
    static Result runSidebandImpulse (int sampleRate, int blockSize, float depth,
                                      bool formantOn, double warmupSeconds = 0.5)
    {
        PluginProcessor processor;
        processor.prepareToPlay (sampleRate, blockSize);

        auto setFloatParam = [&] (const juce::String& id, float value)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (processor.apvts.getParameter (id)))
                *p = value;
        };
        auto setBoolParam = [&] (const juce::String& id, bool value)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter (id)))
                *p = value;
        };
        auto setChoiceParam = [&] (const juce::String& id, int index)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
                *p = index;
        };

        if (auto* param = processor.apvts.getParameter (params::IDs::formantEnabled))
            param->setValueNotifyingHost (formantOn ? 1.0f : 0.0f);
        setChoiceParam (params::IDs::engineVersion, 1);   // Sideband
        for (int b = 1; b <= params::numBands; ++b)
        {
            setFloatParam (params::makeBandParameterID (b, params::IDs::sidebandDepth), depth);
            setFloatParam (params::makeBandParameterID (b, params::IDs::sidebandFmRatio), 0.3f);
            setFloatParam (params::makeBandParameterID (b, params::IDs::sidebandRoughness), 50.0f);
            setFloatParam (params::makeBandParameterID (b, params::IDs::sidebandLowcut), 300.0f);
            setBoolParam (params::makeBandParameterID (b, params::IDs::sidebandEnable), true);
        }

        return runImpulseWith (processor, sampleRate, blockSize, warmupSeconds);
    }

    /** Shared impulse-render body (extracted so both engines use one path). */
    static Result runImpulseWith (PluginProcessor& processor, int sampleRate, int blockSize,
                                  double warmupSeconds = 0.5)
    {
        const auto total = static_cast<int> (sampleRate * (warmupSeconds + 1.0));
        const auto impulsePos = static_cast<int> (sampleRate * warmupSeconds);
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;

        Result r;
        int pos = 0;
        while (pos < total)
        {
            buf.clear();
            if (impulsePos >= pos && impulsePos < pos + blockSize)
                buf.setSample (0, impulsePos - pos, 1.0f);

            processor.processBlock (buf, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = buf.getSample (0, i);
                r.finite = r.finite && std::isfinite (v);
                const auto a = std::abs (v);
                if (a > 0.2f)
                    ++r.significant;
                if (a > r.peak)
                {
                    r.peak = a;
                    r.peakIndex = pos + i - impulsePos;
                }
            }
            pos += blockSize;
        }

        r.reported = processor.getLatencySamples();
        return r;
    }

    void runTest() override
    {
        // L01 / L02: both states at three sample rates (block 512).
        for (double sr : { 44100.0, 96000.0, 192000.0 })
        {
            for (bool formantOn : { false, true })
            {
                const auto label = juce::String (formantOn ? "on " : "off")
                                 + " @" + juce::String (static_cast<int> (sr));

                beginTest (juce::String ("L0") + (formantOn ? "2" : "1")
                           + " impulse latency " + label);
                {
                    const auto r = runImpulse (static_cast<int> (sr), 512, formantOn);
                    const auto expected = formantOn ? 2048 : 0;
                    const auto delta = std::abs (r.peakIndex - expected);

                    logMessage (juce::String ("  reported=") + juce::String (r.reported)
                                + " measured=" + juce::String (r.peakIndex)
                                + " delta=" + juce::String (delta)
                                + " peak=" + juce::String (r.peak, 4));
                    if (formantOn)
                        expect (r.reported == 2048, "reported latency != 2048 with formant on");
                    else
                        expect (r.reported == 0, "reported latency != 0 with formant off");

                    // R11 record: the exciter oversampling group delay shows
                    // up as the small positive offset in the off-state.
                    expect (delta <= 8,
                            juce::String ("measured ") + juce::String (r.peakIndex)
                                + " vs expected " + juce::String (expected)
                                + " (" + label + ")");
                    expect (r.finite && r.peak > 0.5f, "impulse response invalid");
                }
            }
        }

        // L04: block-size independence (48 kHz, both states).
        for (bool formantOn : { false, true })
        {
            beginTest (juce::String ("L04 block scan, formant ") + (formantOn ? "on" : "off"));
            {
                const auto reference = runImpulse (48000, 512, formantOn);
                for (int block : { 1, 7, 64, 8192 })
                {
                    const auto r = runImpulse (48000, block, formantOn);
                    expect (r.peakIndex == reference.peakIndex,
                            juce::String ("block ") + juce::String (block)
                                + " peak " + juce::String (r.peakIndex)
                                + " != block 512 peak " + juce::String (reference.peakIndex));
                }
            }
        }

        // L03: switching formant_enabled mid-stream updates the report and
        // stays click-free (crossfade, A2/R3).
        beginTest ("L03 mid-stream formant toggle");
        {
            constexpr int sr = 48000;
            constexpr int block = 512;
            PluginProcessor processor;
            processor.prepareToPlay (sr, block);
            if (auto* engine = dynamic_cast<juce::AudioParameterChoice*> (
                    processor.apvts.getParameter (params::IDs::engineVersion)))
                *engine = 0; // T35: L03 pins the legacy engine

            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            const auto total = sr; // 1 s; toggle at 0.4 s
            const auto togglePos = static_cast<int> (0.4 * sr);

            int reportedBefore = -1, reportedAfter = -1;
            bool finite = true;
            float maxAbs = 0.0f;

            int pos = 0;
            while (pos < total)
            {
                buf.clear();
                if (pos == 0)
                    buf.setSample (0, 0, 1.0f);

                if (pos == 0)
                    if (auto* p = processor.apvts.getParameter (params::IDs::formantEnabled))
                        p->setValueNotifyingHost (1.0f);

                processor.processBlock (buf, midi);

                if (pos == 0)
                    reportedBefore = processor.getLatencySamples();

                if (togglePos >= pos && togglePos < pos + block)
                {
                    if (auto* p = processor.apvts.getParameter (params::IDs::formantEnabled))
                        p->setValueNotifyingHost (0.0f);
                }

                for (int i = 0; i < block; ++i)
                {
                    const auto v = buf.getSample (0, i);
                    finite = finite && std::isfinite (v);
                    maxAbs = juce::jmax (maxAbs, std::abs (v));
                }
                pos += block;
            }
            reportedAfter = processor.getLatencySamples();

            expect (reportedBefore == 2048, "latency should report 2048 with formant on");
            expect (reportedAfter == 0, "latency should drop to 0 after formant off");
            expect (finite, "toggle produced non-finite samples");
            expect (maxAbs < 2.0f, "toggle overshoot: " + juce::String (maxAbs));
            logMessage (juce::String ("  toggle: 2048 -> 0, max |x| = ") + juce::String (maxAbs, 4));

            // Review S1: re-measure the ACTUAL latency after the toggle — the
            // off-state must be back to the ~5-sample (exciter) latency.
            {
                const auto post = runImpulse (48000, block, false, /*warmup=*/ 0.2);
                expect (post.peakIndex <= 8,
                        juce::String ("post-toggle measured latency ") + juce::String (post.peakIndex)
                            + " not back to the off-state");
            }
        }

        // L03c (final review O1): rapid formant re-toggle — flip the switch
        // every ~45.8 ms so toggles land inside the hold (42.7 ms) and at
        // the ramp START (the 5 ms ramp lives entirely inside one block, so
        // mid-ramp reversal is unreachable under the block-boundary
        // detection — ramping-disable is exercised right at its start; a
        // block-aligned period like 2*512 would never reach it: 21.3 ms <
        // hold; a plain period like 960 silently degrades to
        // lcm(512, 960) = 160 ms). Verify the output stays finite, bounded,
        // and continuous (no clicks/voids from the LatencyManager state
        // machine's transitions).
        beginTest ("L03c rapid formant re-toggle stays continuous");
        {
            constexpr int sr = 48000;
            constexpr int block = 512;
            PluginProcessor processor;
            processor.prepareToPlay (sr, block);
            if (auto* engine = dynamic_cast<juce::AudioParameterChoice*> (
                    processor.apvts.getParameter (params::IDs::engineVersion)))
                *engine = 0; // T35: L03c pins the legacy engine

            constexpr int total = sr; // 1 s
            constexpr int flipEvery = 2200; // ~45.8 ms: mid-hold + mid-ramp
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            bool enabled = false;
            int nextFlip = 0;
            bool finite = true;
            float maxAbs = 0.0f;
            float prevRms = -1.0f;
            float maxRmsJumpDb = 0.0f;

            int pos = 0;
            while (pos < total)
            {
                for (int i = 0; i < block; ++i)
                {
                    const auto v = 0.2f * std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                                    * static_cast<float> (pos + i) / sr);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }

                // nextFlip plan: toggles land at ~45.8 ms intervals regardless
                // of block alignment (block 0 starts at pos=0).
                if (pos >= nextFlip)
                {
                    enabled = ! enabled;
                    if (auto* p = processor.apvts.getParameter (params::IDs::formantEnabled))
                        p->setValueNotifyingHost (enabled ? 1.0f : 0.0f);
                    nextFlip += flipEvery;
                }

                processor.processBlock (buf, midi);

                double sq = 0.0;
                for (int i = 0; i < block; ++i)
                {
                    const auto v = buf.getSample (0, i);
                    finite = finite && std::isfinite (v);
                    maxAbs = juce::jmax (maxAbs, std::abs (v));
                    sq += static_cast<double> (v) * v;
                }
                const auto rms = static_cast<float> (std::sqrt (sq / block));
                if (prevRms > 0.0f)
                {
                    const auto jump = std::abs (20.0f * std::log10 (rms / juce::jmax (prevRms, 1e-9f)));
                    maxRmsJumpDb = juce::jmax (maxRmsJumpDb, jump);
                }
                prevRms = rms;
                pos += block;
            }

            expect (finite, "rapid toggling produced non-finite samples");
            expect (maxAbs < 1.0f, "rapid toggling overshoot: " + juce::String (maxAbs));
            expect (maxRmsJumpDb < 12.0f,
                    "rapid toggling discontinuity: block RMS jump "
                        + juce::String (maxRmsJumpDb, 2) + " dB");
            logMessage (juce::String ("  rapid toggle: max |x| = ") + juce::String (maxAbs, 4)
                        + ", max block-RMS jump = " + juce::String (maxRmsJumpDb, 2) + " dB");
        }

        // L05 (audit P1-1): the 2048-sample enable hold must already run at
        // the REPORTED latency. An impulse injected shortly BEFORE the enable
        // reaches the output through the delayed dry tap inside the hold
        // (+2048), and a steady-state impulse also lands at +2048. Before the
        // fix the hold output was the undelayed path (42.7 ms early).
        beginTest ("L05 formant enable hold is time-aligned with the reported latency");
        {
            constexpr int sr = 48000;
            constexpr int block = 512;
            PluginProcessor processor;
            processor.prepareToPlay (sr, block);
            if (auto* engine = dynamic_cast<juce::AudioParameterChoice*> (
                    processor.apvts.getParameter (params::IDs::engineVersion)))
                *engine = 0; // Legacy: the hold is a pure formant-latency case

            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;

            const int enablePos = 30 * block;                  // block-aligned edge
            const int impulse1 = enablePos - 1000;             // before enable
            const int impulse2 = enablePos + 6000;             // steady state
            const int total = enablePos + 12000;

            int reportedAfterEnable = -1;
            int peakHold = -1, peakSteady = -1;
            float peakHoldVal = 0.0f, peakSteadyVal = 0.0f;
            bool finite = true;

            int pos = 0;
            while (pos < total)
            {
                buf.clear();
                for (int i = 0; i < block; ++i)
                {
                    const auto abs = pos + i;
                    if (abs == impulse1 || abs == impulse2)
                        buf.setSample (0, i, 1.0f);
                }

                if (pos == enablePos)
                    if (auto* p = processor.apvts.getParameter (params::IDs::formantEnabled))
                        p->setValueNotifyingHost (1.0f);

                processor.processBlock (buf, midi);

                if (pos == enablePos)
                    reportedAfterEnable = processor.getLatencySamples();

                for (int i = 0; i < block; ++i)
                {
                    const auto abs = pos + i;
                    const auto v = buf.getSample (0, i);
                    finite = finite && std::isfinite (v);

                    // Hold window: from the enable block to just before the
                    // steady-state impulse.
                    if (abs >= enablePos && abs < impulse2)
                    {
                        if (std::abs (v) > peakHoldVal)
                        {
                            peakHoldVal = std::abs (v);
                            peakHold = abs;
                        }
                    }
                    if (abs >= impulse2)
                    {
                        if (std::abs (v) > peakSteadyVal)
                        {
                            peakSteadyVal = std::abs (v);
                            peakSteady = abs;
                        }
                    }
                }
                pos += block;
            }

            expectEquals (reportedAfterEnable, 2048,
                          "latency must be reported from the enable block");
            expect (finite && peakHoldVal > 0.5f, "hold impulse response invalid");
            expectWithinAbsoluteError (static_cast<double> (peakHold - impulse1), 2048.0, 8.0,
                                       "hold output is not aligned to the reported 2048");
            expectWithinAbsoluteError (static_cast<double> (peakSteady - impulse2), 2048.0, 8.0,
                                       "steady-state formant output is not aligned to 2048");
            logMessage (juce::String ("  hold impulse delta = ")
                        + juce::String (peakHold - impulse1)
                        + ", steady impulse delta = " + juce::String (peakSteady - impulse2));
        }

        // L06 (audit P1-2): the latency report must be refreshable outside
        // processBlock (state/preset load while stopped) and re-published on
        // a reused instance's prepare, even when the new total equals the
        // freshly-cleared internal value.
        beginTest ("L06 latency report refreshes on state load and re-prepare");
        {
            constexpr int sr = 48000;
            constexpr int block = 512;
            const int lmax = dsp::SidebandEngine::maxLatencySamplesForRate (sr);

            auto setChoice = [] (PluginProcessor& p, const char* id, int index)
            {
                if (auto* x = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)))
                    *x = index;
            };
            auto setBool = [] (PluginProcessor& p, const char* id, bool value)
            {
                if (auto* x = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter (id)))
                    *x = value;
            };
            auto captureXml = [] (PluginProcessor& p)
            {
                juce::MemoryBlock block;
                p.getStateInformation (block);
                return juce::AudioProcessor::getXmlFromBinary (block.getData(),
                                                               static_cast<int> (block.getSize()));
            };

            // Steady states: Sideband + formant, then Legacy + formant off.
            PluginProcessor sideband;
            setChoice (sideband, "engine_version", 1);
            setBool (sideband, "formant_enabled", true);
            sideband.prepareToPlay (sr, block);
            expectEquals (sideband.getLatencySamples(), 2048 + lmax,
                          "Sideband + formant must report 2048 + Lmax");
            auto sidebandXml = captureXml (sideband);

            PluginProcessor legacy;
            setChoice (legacy, "engine_version", 0);
            setBool (legacy, "formant_enabled", false);
            legacy.prepareToPlay (sr, block);
            expectEquals (legacy.getLatencySamples(), 0);
            auto legacyXml = captureXml (legacy);

            expect (sidebandXml != nullptr && legacyXml != nullptr, "state capture failed");
            if (sidebandXml == nullptr || legacyXml == nullptr)
                return;

            // (a) Loaded while stopped: no processBlock may be needed.
            legacy.applyStateXml (*sidebandXml);
            expectEquals (legacy.getLatencySamples(), 2048 + lmax,
                          "state load must publish the new latency immediately");

            legacy.applyStateXml (*legacyXml);
            expectEquals (legacy.getLatencySamples(), 0,
                          "state load back to Legacy must clear the report immediately");

            // (b) Reused instance: prepare() clears the internal atomic, so
            // the first prepareBlock must force a publish even when the new
            // total is 0 and the report was 2048 + Lmax before.
            PluginProcessor reused;
            setChoice (reused, "engine_version", 1);
            setBool (reused, "formant_enabled", true);
            reused.prepareToPlay (sr, block);
            expectEquals (reused.getLatencySamples(), 2048 + lmax);

            // Change the parameters directly (no applyStateXml refresh) so the
            // only publish path is prepare -> prepareBlock's forced report.
            setChoice (reused, "engine_version", 0);
            setBool (reused, "formant_enabled", false);
            reused.prepareToPlay (sr, block);
            expectEquals (reused.getLatencySamples(), 0,
                          "re-prepare must clear a stale latency report");
        }

        // R11 record: the oversampling group delay (off-state measured offset)
        // for the report file.
        beginTest ("R11 oversampling group-delay record");
        {
            const auto r = runImpulse (48000, 512, false);
            logMessage (juce::String ("  exciter oversampling group delay (48k, off): ")
                        + juce::String (r.peakIndex) + " samples (reported as 0, SPEC §5.1/R11)");
            expect (r.peakIndex <= 8, "off-state latency exceeded the 8-sample budget");
        }

        // SB-L01 (T31/T45): sideband engaged, input→output group delay. The
        // impulse keeps the wet path unvoiced, so the peak sits at the fixed
        // dry baseline Lmax = A(100 %) + fog + 4 (T45: depth-independent,
        // budgeted — SB-U27), which is exactly what the design reports.
        // Budget: Lmax + 5 samples of margin, in ms at every rate.
        beginTest ("SB-L01 sideband group delay stays within budget (depth × rate)");
        {
            const double lAt100 = static_cast<double> (
                dsp::SidebandEngine::maxLatencySamplesForRate (48000.0));
            const double budgetMs = (lAt100 + 5.0) * 1000.0 / 48000.0;

            for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                int prevPeak = -1;
                for (float depth : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
                {
                    const auto r = runSidebandImpulse (static_cast<int> (sr), 512, depth, false);
                    const auto delayMs = static_cast<double> (r.peakIndex) * 1000.0 / sr;
                    logMessage (juce::String ("  sr=") + juce::String (static_cast<int> (sr))
                                + " depth=" + juce::String (depth, 0)
                                + " measured=" + juce::String (r.peakIndex)
                                + " samples (" + juce::String (delayMs, 3) + " ms)");
                    expect (delayMs <= budgetMs,
                            juce::String ("sr=") + juce::String (static_cast<int> (sr))
                                + " depth=" + juce::String (depth, 0)
                                + " delay " + juce::String (delayMs, 3) + " ms > 0.833 ms budget");
                    // Catmull-Rom spreads the impulse over 4 taps at
                    // fractional L — the peak can drop to ~0.4; the delay
                    // POSITION (above) is what the acceptance measures.
                    expect (r.finite && r.peak > 0.25f,
                            juce::String ("sideband impulse response invalid: peak=")
                                + juce::String (r.peak, 4)
                                + " finite=" + juce::String (r.finite ? "y" : "n")
                                + " sr=" + juce::String (static_cast<int> (sr))
                                + " depth=" + juce::String (depth, 0));
                    if (prevPeak >= 0)
                        expect (r.peakIndex >= prevPeak,
                                juce::String ("depth ") + juce::String (depth, 0)
                                    + " delay regressed vs depth "
                                    + juce::String (prevPeak)
                                    + " (L must grow monotonically with depth)");
                    prevPeak = r.peakIndex;
                }
            }
        }

        // SB-L02 (T31/T45): the REPORTED latency depends only on the engine
        // and formant state (Legacy 0/2048; Sideband Lmax / 2048+Lmax), never
        // on depth/band toggles (SPEC §5.1, SIDEBAND_SPEC §9.1).
        beginTest ("SB-L02 reported latency follows engine/formant (constant Lmax)");
        {
            for (int engine : { 0, 1 })
            {
                for (float depth : { 0.0f, 100.0f })
                {
                    for (bool formantOn : { false, true })
                    {
                        const auto r = runSidebandImpulse (48000, 512, depth, formantOn);
                        // engine=0 path: runSidebandImpulse forces engine=1 —
                        // re-run with the legacy engine for the matrix row.
                        if (engine == 0)
                        {
                            PluginProcessor processor;
                            processor.prepareToPlay (48000, 512);
                            if (auto* p = processor.apvts.getParameter (params::IDs::formantEnabled))
                                p->setValueNotifyingHost (formantOn ? 1.0f : 0.0f);
                            if (auto* eng = dynamic_cast<juce::AudioParameterChoice*> (
                                    processor.apvts.getParameter (params::IDs::engineVersion)))
                                *eng = 0; // T35: legacy matrix row
                            const auto rl = runImpulseWith (processor, 48000, 512);
                            expect (rl.reported == (formantOn ? 2048 : 0),
                                    juce::String ("legacy engine=0 depth=") + juce::String (depth, 0)
                                        + " formant=" + (formantOn ? "on" : "off")
                                        + " reported " + juce::String (rl.reported));
                            continue;
                        }
                        // T45: engine=1 reports the fixed sideband baseline
                        // Lmax = A(100 %) + fog + 4 for every depth / band
                        // toggle; the dry and Bypass paths use the same value.
                        const int sidebandL =
                            dsp::SidebandEngine::maxLatencySamplesForRate (48000.0);
                        expect (r.reported == (formantOn ? 2048 : 0) + sidebandL,
                                juce::String ("engine=1 depth=") + juce::String (depth, 0)
                                    + " formant=" + (formantOn ? "on" : "off")
                                    + " reported " + juce::String (r.reported)
                                    + " (expected " + juce::String ((formantOn ? 2048 : 0) + sidebandL)
                                    + ", T45: fixed Lmax reported)");
                    }
                }
            }
        }

        // T45: the fixed Lmax must be the same for Band On/off, Sideband On/off,
        // hidden bands, Dry/Wet and Bypass; dry/Bypass must be latency-aligned.
        beginTest ("T45 reported latency == measured output across bypass/drywet/band toggles");
        {
            constexpr int sr = 48000;
            constexpr int block = 512;
            const int lmax = dsp::SidebandEngine::maxLatencySamplesForRate (sr);

            auto setFloat = [] (PluginProcessor& p, const char* id, float value)
            {
                if (auto* x = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (id)))
                    *x = value;
            };
            auto setBool = [] (PluginProcessor& p, const char* id, bool value)
            {
                if (auto* x = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter (id)))
                    *x = value;
            };
            auto setChoice = [] (PluginProcessor& p, const char* id, int index)
            {
                if (auto* x = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)))
                    *x = index;
            };
            auto run = [&] (auto configure)
            {
                PluginProcessor p;
                // T62: default is Legacy — pin Sideband for this matrix.
                if (auto* x = dynamic_cast<juce::AudioParameterChoice*> (
                        p.apvts.getParameter ("engine_version")))
                    *x = 1;
                p.prepareToPlay (sr, block);
                configure (p, setFloat, setBool, setChoice);
                return runImpulseWith (p, sr, block, 0.2);
            };

            // Initial report must already be correct before the first block.
            {
                PluginProcessor p;
                setChoice (p, "engine_version", 1);
                p.prepareToPlay (sr, block);
                expectEquals (p.getLatencySamples(), lmax,
                              "initial latency must match the explicit Sideband baseline");
            }

            const auto defaultR = run ([] (PluginProcessor&, auto&, auto&, auto&) {});
            const auto allSbOff = run ([] (PluginProcessor& p, auto&, auto& setB, auto&)
            {
                setB (p, "b1_sideband_enable", false);
                setB (p, "b2_sideband_enable", false);
                setB (p, "b3_sideband_enable", false);
            });
            const auto bandOff = run ([] (PluginProcessor& p, auto&, auto& setB, auto&)
            {
                setB (p, "b1_enable", false);
            });
            const auto hidden = run ([] (PluginProcessor& p, auto& setF, auto&, auto& setC)
            {
                setC (p, "band_count", 0);
                setF (p, "b3_sideband_depth", 100.0f);
            });
            const auto dryWet50 = run ([] (PluginProcessor& p, auto& setF, auto&, auto&)
            {
                setF (p, "dry_wet", 50.0f);
            });
            const auto bypass = run ([] (PluginProcessor& p, auto&, auto& setB, auto&)
            {
                setB (p, "bypass", true);
            });

            for (const auto* pair : { &defaultR, &allSbOff, &bandOff, &hidden, &dryWet50, &bypass })
            {
                logMessage (juce::String ("  T45 peak=") + juce::String (pair->peakIndex)
                            + " reported=" + juce::String (pair->reported)
                            + " significant=" + juce::String (pair->significant));
                expectEquals (pair->reported, lmax, "Sideband report must stay fixed at Lmax");
                expectWithinAbsoluteError (static_cast<double> (pair->peakIndex),
                                           static_cast<double> (lmax), 1.0,
                                           "measured output is not aligned to Lmax");
                expectEquals (pair->significant, 1, "expected one aligned impulse");
            }

            // Bypass + formant: the bypassed audio must carry formant + Lmax.
            const auto bypassFormant = run ([] (PluginProcessor& p, auto&, auto& setB, auto&)
            {
                setB (p, "bypass", true);
                setB (p, "formant_enabled", true);
            });
            expectEquals (bypassFormant.reported, 2048 + lmax);
            expectWithinAbsoluteError (static_cast<double> (bypassFormant.peakIndex),
                                       static_cast<double> (2048 + lmax), 8.0,
                                       "bypass+formant output is not aligned to 2048+Lmax");
            expectEquals (bypassFormant.significant, 1, "bypass+formant should be one impulse");

            // Legacy bypass + formant must be aligned to 2048 (no sideband L).
            const auto legacyBypassFormant = run ([] (PluginProcessor& p, auto&, auto& setB, auto& setC)
            {
                setC (p, "engine_version", 0);
                setB (p, "bypass", true);
                setB (p, "formant_enabled", true);
            });
            expectEquals (legacyBypassFormant.reported, 2048);
            expectWithinAbsoluteError (static_cast<double> (legacyBypassFormant.peakIndex),
                                       2048.0, 8.0,
                                       "legacy bypass+formant output is not aligned to 2048");
            expectEquals (legacyBypassFormant.significant, 1,
                          "legacy bypass+formant should be one impulse");

            // 100 % wet + formant: reported and measured must both be 2048+Lmax.
            const auto formantWet = run ([] (PluginProcessor& p, auto&, auto& setB, auto&)
            {
                setB (p, "formant_enabled", true);
            });
            expectEquals (formantWet.reported, 2048 + lmax);
            expectWithinAbsoluteError (static_cast<double> (formantWet.peakIndex),
                                       static_cast<double> (2048 + lmax), 8.0,
                                       "formant wet path is not aligned to 2048+Lmax");

            // Depth automation must not change the report.
            {
                PluginProcessor p;
                setChoice (p, "engine_version", 1);   // T62: explicit Sideband
                p.prepareToPlay (sr, block);
                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer midi;

                setFloat (p, "b1_sideband_depth", 0.0f);
                p.processBlock (buf, midi);
                expectEquals (p.getLatencySamples(), lmax);
                setFloat (p, "b1_sideband_depth", 100.0f);
                p.processBlock (buf, midi);
                expectEquals (p.getLatencySamples(), lmax,
                              "depth automation must not change the reported latency");
            }
        }
    }
};

LatencyMeasureTests latencyMeasureTests;

} // namespace
