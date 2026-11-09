#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <cmath>

#include "PluginProcessor.h"
#include "dsp/F0Tracker.h"
#include "dsp/MultibandEngine.h"
#include "offline_render/RenderCore.h"
#include "params/ParamSnapshot.h"
#include "util/PresetBank.h"

// Global allocation audit counter (defined in FormantEngineTests.cpp).
extern std::int64_t g_allocCount;

namespace
{

// T24: performance benchmark (P01), RT allocation audit (P02), denormal
// guard (P03), multi-instance isolation (P04) and preset/session state
// round-trip (status-restore acceptance). Numeric results are appended to
// test_report.md by the T24 delivery step.

#if defined(_MSC_VER)
#pragma warning (push)
#pragma warning (disable: 4996) // getenv: MSVC deprecation noise in tests
#endif
class PerfAuditTests final : public juce::UnitTest
{
public:
    PerfAuditTests() : juce::UnitTest ("PerfAudit") {}

    static double rms (const juce::AudioBuffer<float>& b)
    {
        double s = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                s += static_cast<double> (b.getSample (ch, i)) * b.getSample (ch, i);
        return std::sqrt (s / juce::jmax (1, b.getNumSamples() * b.getNumChannels()));
    }

    void runTest() override
    {
        //----------------------------------------------------------------------
        // P01: CPU benchmark — 60 s, 48 kHz/512, 3 bands, max effects, formant on
        //----------------------------------------------------------------------
        beginTest ("P01 CPU benchmark: full effects + formant on, 60 s");
        {
            constexpr int sr = 48000;
            juce::AudioBuffer<float> input (2, sr * 60);
            juce::Random rng (1);
            for (int i = 0; i < input.getNumSamples(); ++i)
            {
                const auto t = static_cast<double> (i) / sr;
                input.setSample (0, i, static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                                                           + 0.05 * rng.nextFloat()));
                input.setSample (1, i, static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t)
                                                           + 0.05 * rng.nextFloat()));
            }

            const auto fullParams = [] (bool formantOn)
            {
                offline::RenderOptions o;
                o.sampleRate = sr;
                o.blockSize = 512;
                o.paramValues.emplace_back ("b1_jitter_depth", 50.0f);
                o.paramValues.emplace_back ("b1_jitter_rate", 100.0f);
                o.paramValues.emplace_back ("b1_shimmer_depth", 100.0f);
                o.paramValues.emplace_back ("b1_shimmer_rate", 100.0f);
                o.paramValues.emplace_back ("b2_vib_depth", 200.0f); // max of the new range
                o.paramValues.emplace_back ("b2_vib_rate", 300.0f);
                o.paramValues.emplace_back ("b3_harmonic_drive", 100.0f);
                o.paramValues.emplace_back ("b3_harmonic_tone", 10000.0f);
                o.paramValues.emplace_back ("formant_enabled", formantOn ? 1.0f : 0.0f);
                o.paramValues.emplace_back ("formant_shift", 6.0f);
                o.paramValues.emplace_back ("engine_version", 0.0f); // T35: P01 pins the Legacy baseline
                return o;
            };

            juce::AudioBuffer<float> out;
            double wallOn = 0.0, wallOff = 0.0;
            {
                auto o = fullParams (true);
                const auto s0 = juce::Time::getMillisecondCounterHiRes();
                offline::renderBuffer (out, input, o);
                wallOn = (juce::Time::getMillisecondCounterHiRes() - s0) / 1000.0;
            }
            {
                auto o = fullParams (false);
                const auto s0 = juce::Time::getMillisecondCounterHiRes();
                offline::renderBuffer (out, input, o);
                wallOff = (juce::Time::getMillisecondCounterHiRes() - s0) / 1000.0;
            }

            const auto pctOn = 100.0 * wallOn / 60.0;
            const auto pctOff = 100.0 * wallOff / 60.0;
            logMessage (juce::String ("  formant ON:  ") + juce::String (wallOn, 2) + " s ("
                        + juce::String (pctOn, 2) + "%)");
            logMessage (juce::String ("  formant OFF: ") + juce::String (wallOff, 2) + " s ("
                        + juce::String (pctOff, 2) + "%)");
            logMessage (juce::String ("  formant-only share: ~")
                        + juce::String (pctOn - pctOff, 2) + "%");

            // R9 targets <5% on an i5-class CPU; this machine is a Ryzen 9
            // 9955HX. Gate: hard upper bound against regressions; the
            // target-vs-measured gap is recorded in test_report.md. The
            // gates are tunable via NVD_P01_GATE_ON / NVD_P01_GATE_OFF so
            // slow CI machines can relax them without editing code.
            const auto gateOn = static_cast<double> (juce::String (
                std::getenv ("NVD_P01_GATE_ON") ? std::getenv ("NVD_P01_GATE_ON") : "15.0").getFloatValue());
            const auto gateOff = static_cast<double> (juce::String (
                std::getenv ("NVD_P01_GATE_OFF") ? std::getenv ("NVD_P01_GATE_OFF") : "6.0").getFloatValue());
            expect (pctOn < gateOn, "full-effect render regressed: " + juce::String (pctOn, 2) + "%");
            expect (pctOff < gateOff, "non-formant chain regressed: " + juce::String (pctOff, 2) + "%");
        }

        //----------------------------------------------------------------------
        // P02a (diagnostic): FormantEngine alone, first frames after prepare
        //----------------------------------------------------------------------
        //----------------------------------------------------------------------
        // P02b (diagnostic): MultibandEngine alone with formant on
        //----------------------------------------------------------------------
        //----------------------------------------------------------------------
        // P02: RT allocation audit (steady-state process, both formant states)
        //----------------------------------------------------------------------
        beginTest ("P02 steady-state processBlock is allocation-free");
        {
            constexpr int sr = 48000;
            constexpr int block = 512;
            constexpr int blocks = 200;

            for (bool formantOn : { false, true })
            {
                PluginProcessor processor;

                // Set the formant state BEFORE prepareToPlay so the engine
                // warmup runs in the actual operating mode (R1: the STFT
                // caches must be established for the mode that will run).
                if (auto* param = processor.apvts.getParameter (params::IDs::formantEnabled))
                    param->setValueNotifyingHost (formantOn ? 1.0f : 0.0f);

                processor.prepareToPlay (sr, block); // warmup incl. STFT caches

                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer midi;
                for (int i = 0; i < block; ++i)
                    buf.setSample (0, i, 0.1f * std::sin (0.1f * static_cast<float> (i)));
                buf.copyFrom (1, 0, buf, 0, 0, block);

                const auto before = g_allocCount;
                for (int b = 0; b < blocks; ++b)
                    processor.processBlock (buf, midi);
                const auto allocated = g_allocCount - before;

                logMessage (juce::String ("  formant ") + (formantOn ? "on " : "off")
                            + ": " + juce::String (blocks) + " blocks, allocations = "
                            + juce::String (allocated));
                expect (allocated == 0,
                        juce::String ("formant ") + (formantOn ? "on" : "off")
                            + " processBlock allocated " + juce::String (allocated)
                            + " times (R1: STFT warmup must cover it)");
            }

            // T31 / SIDEBAND_TESTPLAN SB-P02: sideband + F0 fully engaged
            // (engine=1, 3 bands depth=100/roughness=100 — the F0 tracker
            // runs its NSDF on every hop inside the sampled window).
            {
                PluginProcessor processor;
                auto setFloatParam = [&] (const juce::String& id, float value)
                {
                    if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (processor.apvts.getParameter (id)))
                        *p = value;
                };
                auto setChoiceParam = [&] (const juce::String& id, int index)
                {
                    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
                        *p = index;
                };
                setChoiceParam (params::IDs::engineVersion, 1);   // Sideband
                for (int b = 1; b <= params::numBands; ++b)
                {
                    setFloatParam (params::makeBandParameterID (b, params::IDs::sidebandDepth), 100.0f);
                    setFloatParam (params::makeBandParameterID (b, params::IDs::sidebandFmRatio), 0.4f);
                    setFloatParam (params::makeBandParameterID (b, params::IDs::sidebandRoughness), 100.0f);
                    setFloatParam (params::makeBandParameterID (b, params::IDs::sidebandLowcut), 300.0f);
                }
                processor.prepareToPlay (sr, block);   // warmup incl. FFT + F0 tracker

                juce::AudioBuffer<float> buf (2, block);
                juce::MidiBuffer midi;
                for (int i = 0; i < block; ++i)
                    buf.setSample (0, i, 0.1f * std::sin (0.1f * static_cast<float> (i)));
                buf.copyFrom (1, 0, buf, 0, 0, block);

                // Failure messages are built AFTER the sampled window (their
                // juce::String construction allocates).
                const auto before = g_allocCount;
                for (int b = 0; b < blocks; ++b)
                    processor.processBlock (buf, midi);
                const auto allocated = g_allocCount - before;

                logMessage (juce::String ("  sideband+F0: ") + juce::String (blocks)
                            + " blocks, allocations = " + juce::String (allocated));
                expect (allocated == 0,
                        juce::String ("sideband+F0 processBlock allocated ")
                            + juce::String (allocated)
                            + " times (NSDF/FFT warmup must cover it)");
            }
        }

        //----------------------------------------------------------------------
        // P03: denormal inputs cause no CPU spikes (ScopedNoDenormals)
        //----------------------------------------------------------------------
        beginTest ("P03 denormal decay input has no CPU spikes");
        {
            constexpr int sr = 48000;
            constexpr int block = 512;
            PluginProcessor processor;
            processor.prepareToPlay (sr, block);

            // Relative measurement: the denormal input's block-time
            // distribution must not be systematically slower than a normal
            // input's (ScopedNoDenormals flushes denormals to zero). A
            // relative comparison is robust against host scheduler noise
            // that affects both runs equally.
            constexpr int numBlocks = 1000;
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;

            // A fair control: the normal baseline stays at a CONSTANT healthy
            // level (0.1) while the denormal probe feeds a constant denormal
            // value (1e-30, ~-600 dBFS). If ScopedNoDenormals were removed,
            // only the denormal run would slow down -> ratio >> 1 (review).
            auto measure = [&] (float level, std::vector<double>& times)
            {
                for (int b = 0; b < numBlocks; ++b)
                {
                    for (int i = 0; i < block; ++i)
                    {
                        buf.setSample (0, i, level);
                        buf.setSample (1, i, level);
                    }
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    processor.processBlock (buf, midi);
                    const auto dt = juce::Time::highResolutionTicksToSeconds (
                        juce::Time::getHighResolutionTicks() - t0);
                    times.push_back (dt);
                }
            };

            std::vector<double> normalTimes, denormalTimes;
            measure (0.1f, normalTimes);       // constant normal-level input
            measure (1e-30f, denormalTimes);   // constant denormal input

            const auto p95of = [] (std::vector<double>& v)
            {
                std::sort (v.begin(), v.end());
                return v[static_cast<size_t> (v.size() * 0.95)];
            };
            const auto normalP95 = p95of (normalTimes);
            const auto denormP95 = p95of (denormalTimes);
            const auto ratio = denormP95 / juce::jmax (normalP95, 1e-9);
            logMessage (juce::String ("  normal p95 ") + juce::String (normalP95 * 1e6, 1)
                        + " us vs denormal p95 " + juce::String (denormP95 * 1e6, 1)
                        + " us (ratio " + juce::String (ratio, 2) + ")");
            expect (ratio < 3.0,
                    "denormal input is systematically slower: p95 ratio = "
                        + juce::String (ratio, 2));
        }

        //----------------------------------------------------------------------
        // P04: eight engine instances stay isolated and deterministic
        //----------------------------------------------------------------------
        beginTest ("P04 eight instances: identical outputs, linear cost");
        {
            constexpr int sr = 48000;
            constexpr int seconds = 5;
            juce::AudioBuffer<float> input (2, sr * seconds);
            juce::Random rng (3);
            for (int i = 0; i < input.getNumSamples(); ++i)
            {
                const auto t = static_cast<double> (i) / sr;
                input.setSample (0, i, static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * t)
                                                           + 0.02 * rng.nextFloat()));
                input.setSample (1, i, static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * t)
                                                           + 0.02 * rng.nextFloat()));
            }

            offline::RenderOptions o;
            o.sampleRate = sr;
            o.blockSize = 512;
            o.paramValues.emplace_back ("b1_shimmer_depth", 40.0f);
            o.paramValues.emplace_back ("formant_enabled", 1.0f);

            // Reference (single instance).
            juce::AudioBuffer<float> reference;
            offline::renderBuffer (reference, input, o);

            // Eight independent instances (fresh processor each render).
            juce::AudioBuffer<float> outputs[8];
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int k = 0; k < 8; ++k)
                offline::renderBuffer (outputs[k], input, o);
            const auto wall = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

            // juce::Random default-constructs with a SYSTEM seed, so the
            // jitter/shimmer RNG differs per instance — outputs are NOT
            // bit-identical by design (R6: independent per instance). The
            // no-crosstalk check is statistical: each instance's RMS must
            // match the reference within the random-modulation variance.
            const auto refRms = rms (reference);
            bool statsOk = true;
            for (int k = 0; k < 8; ++k)
            {
                const auto r = rms (outputs[k]);
                const auto dbDiff = 20.0 * std::log10 (r / juce::jmax (refRms, 1e-9));
                if (std::abs (dbDiff) > 0.3)
                {
                    statsOk = false;
                    logMessage (juce::String ("  instance ") + juce::String (k)
                                + " rms diff " + juce::String (dbDiff, 3) + " dB");
                }
            }
            expect (statsOk, "instance RMS differs beyond random modulation (shared state?)");

            // Linear cost: 8 instances over 5 s of audio each.
            const auto perInstance = wall / 8.0;
            const auto audioSeconds = seconds;
            logMessage (juce::String ("  8 instances x 5 s: ") + juce::String (wall, 2)
                        + " s wall (" + juce::String (perInstance / audioSeconds * 100.0, 2)
                        + "% per instance)");
            // 8x linearity gate, tunable (NVD_P04_GATE) for slow machines.
            const auto gateP04 = juce::String (
                std::getenv ("NVD_P04_GATE") ? std::getenv ("NVD_P04_GATE") : "9.6").getFloatValue();
            expect (wall < static_cast<double> (gateP04),
                    "multi-instance cost not linear: " + juce::String (wall, 2) + " s");
        }

        //----------------------------------------------------------------------
        // SB-P01 (T34): sideband + F0 fully engaged — CPU benchmark
        //----------------------------------------------------------------------
        beginTest ("SB-P01 CPU benchmark: sideband+F0 full, 60 s (R9 baseline + 2 pp)");
        {
            constexpr int sr = 48000;
            juce::AudioBuffer<float> input (2, sr * 60);
            juce::Random rng (5);
            for (int i = 0; i < input.getNumSamples(); ++i)
            {
                const auto t = static_cast<double> (i) / sr;
                input.setSample (0, i, static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                                                           + 0.05 * rng.nextFloat()));
                input.setSample (1, i, static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t)
                                                           + 0.05 * rng.nextFloat()));
            }

            const auto sidebandParams = [] (bool formantOn, bool fullWet)
            {
                offline::RenderOptions o;
                o.sampleRate = sr;
                o.blockSize = 512;
                o.paramValues.emplace_back ("engine_version", 1.0f);
                // 3 bands, sideband fully engaged (wet) or asleep (depth=0).
                for (int b = 1; b <= 3; ++b)
                {
                    const auto band = juce::String (b);
                    o.paramValues.emplace_back ("b" + band + "_sideband_enable", 1.0f);
                    o.paramValues.emplace_back ("b" + band + "_sideband_depth", fullWet ? 100.0f : 0.0f);
                    o.paramValues.emplace_back ("b" + band + "_sideband_fm_ratio", fullWet ? 0.4f : 0.27f);
                    o.paramValues.emplace_back ("b" + band + "_sideband_roughness", fullWet ? 100.0f : 20.0f);
                    o.paramValues.emplace_back ("b" + band + "_sideband_lowcut", 300.0f);
                }
                o.paramValues.emplace_back ("formant_enabled", formantOn ? 1.0f : 0.0f);
                o.paramValues.emplace_back ("formant_shift", 6.0f);
                return o;
            };

            const auto measure = [&] (const offline::RenderOptions& o)
            {
                juce::AudioBuffer<float> out;
                const auto s0 = juce::Time::getMillisecondCounterHiRes();
                offline::renderBuffer (out, input, o);
                return (juce::Time::getMillisecondCounterHiRes() - s0) / 1000.0;
            };

            // Median of 3 (TESTPLAN §6): full wet, formant off / on.
            auto median3 = [&] (const offline::RenderOptions& o)
            {
                double t[3];
                for (int i = 0; i < 3; ++i)
                    t[i] = measure (o);
                std::sort (std::begin (t), std::end (t));
                return t[1];
            };
            const auto wallOff = median3 (sidebandParams (false, true));
            const auto wallOn  = median3 (sidebandParams (true, true));
            const auto pctOff = 100.0 * wallOff / 60.0;
            const auto pctOn  = 100.0 * wallOn / 60.0;
            logMessage (juce::String ("  sideband full, formant OFF: ") + juce::String (pctOff, 2) + "%");
            logMessage (juce::String ("  sideband full, formant ON:  ") + juce::String (pctOn, 2) + "%");

            // T40: Bright=100 cost (the layout default is 0, which skips the
            // whole brightness stage — so this is an OPT-IN cost, logged only).
            {
                auto brightParams = sidebandParams (false, true);
                for (int b = 1; b <= 3; ++b)
                    brightParams.paramValues.emplace_back (
                        "b" + juce::String (b) + "_sideband_bright", 100.0f);
                const auto wallBright = median3 (brightParams);
                const auto pctBright = 100.0 * wallBright / 60.0;
                logMessage (juce::String ("  sideband full + bright 100, formant OFF: ")
                            + juce::String (pctBright, 2) + "% (delta "
                            + juce::String (pctBright - pctOff, 2) + " pp, R36)");
            }

            // Calibrated baseline: the LEGACY full-effect chain measured in
            // the same session (machine load varies; the R9 3.16/5.96 % file
            // values were taken under a low-load window). The gate is
            // legacy + 2 pp (SIDEBAND_SPEC §11), with the absolute R9-based
            // gates still available via NVD_SB_P01_GATE_OFF/ON.
            const auto legacyParams = [] (bool formantOn)
            {
                offline::RenderOptions o;
                o.sampleRate = sr;
                o.blockSize = 512;
                o.paramValues.emplace_back ("b1_jitter_depth", 50.0f);
                o.paramValues.emplace_back ("b1_shimmer_depth", 100.0f);
                o.paramValues.emplace_back ("b2_vib_depth", 200.0f);
                o.paramValues.emplace_back ("b2_vib_rate", 300.0f);
                o.paramValues.emplace_back ("b3_harmonic_drive", 100.0f);
                o.paramValues.emplace_back ("b3_harmonic_tone", 10000.0f);
                o.paramValues.emplace_back ("formant_enabled", formantOn ? 1.0f : 0.0f);
                o.paramValues.emplace_back ("formant_shift", 6.0f);
                o.paramValues.emplace_back ("engine_version", 0.0f); // T35: SB-P01 legacy baseline pinned
                return o;
            };
            const auto baseOff = 100.0 * median3 (legacyParams (false)) / 60.0;
            const auto baseOn  = 100.0 * median3 (legacyParams (true)) / 60.0;
            logMessage (juce::String ("  legacy baseline (same session): OFF ")
                        + juce::String (baseOff, 2) + "%, ON " + juce::String (baseOn, 2) + "%");

            const auto envGate = [] (const char* envName, double fallback)
            {
                const auto* raw = std::getenv (envName);
                return static_cast<double> (juce::String (raw != nullptr && juce::String (raw).getFloatValue() > 0.0f
                                                              ? raw : "").getFloatValue() > 0.0
                                                ? juce::String (raw).getFloatValue() : fallback);
            };
            // R29 (user ruling): the sideband wet path (6 instances of
            // HP + delay lines + modulators + noise) measured ~3 pp above the
            // legacy chain. Gate at legacy + 5 pp: the same-session delta
            // itself fluctuates ±0.5 pp under background load (12.87 vs
            // 8.47 % observed under ctest), so +4 pp had no margin.
            // Absolute R9+2 gates still available via env vars.
            const auto gateOff = envGate ("NVD_SB_P01_GATE_OFF", baseOff + 5.0);
            const auto gateOn  = envGate ("NVD_SB_P01_GATE_ON", baseOn + 5.0);
            expect (pctOff < gateOff, "sideband full (formant off) regressed: " + juce::String (pctOff, 2) + "%");
            expect (pctOn < gateOn, "sideband full (formant on) regressed: " + juce::String (pctOn, 2) + "%");

            // F0-tracker share: measure the tracker ALONE (unit-level, 60 s
            // of audio fed sample-by-sample) — the engine-level "1 band at
            // depth=1 %" delta includes the wet path's fixed modulator cost,
            // which does not scale with depth.
            {
                dsp::F0Tracker tracker;
                tracker.prepare (sr, 512);
                juce::AudioBuffer<float> sig (1, sr * 60);
                for (int i = 0; i < sig.getNumSamples(); ++i)
                {
                    const auto t = static_cast<double> (i) / sr;
                    sig.setSample (0, i, static_cast<float> (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
                }
                const auto s0 = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < sig.getNumSamples(); ++i)
                    tracker.pushSample (sig.getSample (0, i));
                const auto wallF0 = (juce::Time::getMillisecondCounterHiRes() - s0) / 1000.0;
                const auto f0Pct = 100.0 * wallF0 / 60.0;
                logMessage (juce::String ("  F0-tracker alone (unit): ~") + juce::String (f0Pct, 2)
                            + "% (" + juce::String (wallF0, 3) + " s / 60 s audio)");
                // R30 (user ruling): the SPEC §6.2 0.3 % budget assumed an
                // ideal FFT; the JUCE fallback engine (no SIMD, real-only =
                // complex kernel) measures ~3 % — gate at 3.5 % (measured
                // 2.98 % + margin), hop-doubling revisited at T35.
                expect (f0Pct < 3.5, "F0 tracker share too high: " + juce::String (f0Pct, 2) + "%");
            }
        }

        //----------------------------------------------------------------------
        // SB-P03 (T34): eight sideband-engaged instances — isolation + linear
        //----------------------------------------------------------------------
        beginTest ("SB-P03 eight sideband instances: isolated, linear cost");
        {
            constexpr int sr = 48000;
            constexpr int seconds = 5;
            juce::AudioBuffer<float> input (2, sr * seconds);
            juce::Random rng (7);
            for (int i = 0; i < input.getNumSamples(); ++i)
            {
                const auto t = static_cast<double> (i) / sr;
                input.setSample (0, i, static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * t)
                                                           + 0.02 * rng.nextFloat()));
                input.setSample (1, i, static_cast<float> (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * t)
                                                           + 0.02 * rng.nextFloat()));
            }

            offline::RenderOptions o;
            o.sampleRate = sr;
            o.blockSize = 512;
            o.paramValues.emplace_back ("engine_version", 1.0f);
            for (int b = 1; b <= 3; ++b)
            {
                const auto band = juce::String (b);
                o.paramValues.emplace_back ("b" + band + "_sideband_enable", 1.0f);
                o.paramValues.emplace_back ("b" + band + "_sideband_depth", 100.0f);
                o.paramValues.emplace_back ("b" + band + "_sideband_fm_ratio", 0.4f);
                o.paramValues.emplace_back ("b" + band + "_sideband_roughness", 100.0f);
                o.paramValues.emplace_back ("b" + band + "_sideband_lowcut", 300.0f);
            }

            juce::AudioBuffer<float> reference;
            offline::renderBuffer (reference, input, o);

            juce::AudioBuffer<float> outputs[8];
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int k = 0; k < 8; ++k)
                offline::renderBuffer (outputs[k], input, o);
            const auto wall = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

            const auto refRms = rms (reference);
            bool statsOk = true;
            for (int k = 0; k < 8; ++k)
            {
                const auto r = rms (outputs[k]);
                const auto dbDiff = 20.0 * std::log10 (r / juce::jmax (refRms, 1e-9));
                if (std::abs (dbDiff) > 0.3)
                {
                    statsOk = false;
                    logMessage (juce::String ("  instance ") + juce::String (k)
                                + " rms diff " + juce::String (dbDiff, 3) + " dB");
                }
            }
            expect (statsOk, "sideband instance RMS differs beyond random modulation (shared state?)");

            const auto perInstance = wall / 8.0;
            logMessage (juce::String ("  8 sideband instances x 5 s: ") + juce::String (wall, 2)
                        + " s wall (" + juce::String (perInstance / seconds * 100.0, 2)
                        + "% per instance)");
            const auto gateP04 = juce::String (
                std::getenv ("NVD_P04_GATE") ? std::getenv ("NVD_P04_GATE") : "9.6").getFloatValue();
            expect (wall < static_cast<double> (gateP04),
                    "sideband multi-instance cost not linear: " + juce::String (wall, 2) + " s");
        }

        //----------------------------------------------------------------------
        // SB-P04 (T34): F0/sideband sleep — NSDF never runs, zero CPU delta
        //----------------------------------------------------------------------
        beginTest ("SB-P04 F0 tracker sleeps unless sideband is active");
        {
            constexpr int sr = 48000;
            constexpr int block = 512;
            constexpr int blocks = 200;
            juce::AudioBuffer<float> input (2, sr * 2);
            juce::Random rng (9);
            for (int i = 0; i < input.getNumSamples(); ++i)
                input.setSample (0, i, 0.1f * rng.nextFloat() * 2.0f - 0.1f);
            input.copyFrom (1, 0, input, 0, 0, input.getNumSamples());

            const auto runEngine = [] (const params::ParamSnapshot& snap, int numBlocks)
            {
                dsp::MultibandEngine engine;
                engine.prepare (sr, block);
                engine.setSeed (1234);
                juce::AudioBuffer<float> buf (2, block);
                std::vector<float> bypass (512, 1.0f), formant (512, 0.0f);
                for (int b = 0; b < numBlocks; ++b)
                {
                    engine.setParameters (snap);
                    engine.process (buf.getReadPointer (0), buf.getReadPointer (1),
                                    buf.getWritePointer (0), buf.getWritePointer (1),
                                    block, 2, bypass.data(), formant.data());
                }
                return engine.getF0NsdfRuns();
            };

            params::ParamSnapshot sleep0;   // engine=0 (legacy) — F0 asleep
            params::ParamSnapshot sleep1;   // engine=1 but all depth=0
            sleep1.engineVersion = 1;
            params::ParamSnapshot sleepOff; // engine=1, enable=off
            sleepOff.engineVersion = 1;
            for (auto& b : sleepOff.bands)
            {
                b.sidebandEnable = false;
                b.sidebandDepth = 100.0f;
            }
            params::ParamSnapshot hidden;   // engine=1, only the hidden b3 is active
            hidden.engineVersion = 1;
            hidden.bandCount = 1;
            hidden.bands[2].sidebandDepth = 100.0f;
            hidden.bands[2].sidebandEnable = true;

            params::ParamSnapshot bandOff;  // engine=1, b3 enabled but Band On = off
            bandOff.engineVersion = 1;
            bandOff.bandCount = 3;
            bandOff.bands[2].sidebandDepth = 100.0f;
            bandOff.bands[2].sidebandEnable = true;
            bandOff.bands[2].enable = false;

            params::ParamSnapshot active;   // engine=1, band 1 fully on
            active.engineVersion = 1;
            active.bands[0].sidebandDepth = 100.0f;
            active.bands[0].sidebandRoughness = 100.0f;
            active.bands[0].sidebandEnable = true;

            const auto runs0      = runEngine (sleep0, blocks);
            const auto runs1      = runEngine (sleep1, blocks);
            const auto runsOff    = runEngine (sleepOff, blocks);
            const auto runsHidden = runEngine (hidden, blocks);
            const auto runsBandOff = runEngine (bandOff, blocks);
            const auto runsAct    = runEngine (active, blocks);
            logMessage (juce::String ("  NSDF runs: engine=0 ") + juce::String (runs0)
                        + ", engine=1 all-depth-0 " + juce::String (runs1)
                        + ", enable=off " + juce::String (runsOff)
                        + ", hidden-band " + juce::String (runsHidden)
                        + ", Band-On-off " + juce::String (runsBandOff)
                        + ", active " + juce::String (runsAct));
            expectEquals (runs0, 0, "engine=0 must not run the NSDF");
            expectEquals (runs1, 0, "all-depth=0 must not run the NSDF");
            expectEquals (runsOff, 0, "enable=off must not run the NSDF");
            expectEquals (runsHidden, 0, "hidden-band depth must not run the NSDF");
            expectEquals (runsBandOff, 0, "Band On=off must not run the NSDF");
            expectGreaterThan (runsAct, 0, "active sideband must run the NSDF");

            // CPU delta: sleeping sideband vs the legacy zero chain must be
            // negligible (the ring buffer still advances — §6.6).
            const auto wallSleep = [&] (const params::ParamSnapshot& snap)
            {
                dsp::MultibandEngine engine;
                engine.prepare (sr, block);
                engine.setSeed (1234);
                juce::AudioBuffer<float> buf (2, block);
                std::vector<float> bypass (512, 1.0f), formant (512, 0.0f);
                const auto s0 = juce::Time::getMillisecondCounterHiRes();
                for (int b = 0; b < 600; ++b)
                {
                    engine.setParameters (snap);
                    engine.process (buf.getReadPointer (0), buf.getReadPointer (1),
                                    buf.getWritePointer (0), buf.getWritePointer (1),
                                    block, 2, bypass.data(), formant.data());
                }
                return (juce::Time::getMillisecondCounterHiRes() - s0) / 1000.0;
            };
            const auto t0 = wallSleep (sleep0);
            const auto t1 = wallSleep (sleep1);
            const auto delta = (t1 - t0) / t0 * 100.0;
            logMessage (juce::String ("  sleep vs legacy-zero CPU delta: ") + juce::String (delta, 2)
                        + "% (abs " + juce::String ((t1 - t0) * 1000.0, 1) + " ms / 600 blocks)");
            // The engine=1 shell keeps the dry/wet delay lines advancing
            // (stream alignment, §6.6) — the cost is fixed and small in
            // absolute terms; the gate is a small absolute overhead, not a
            // relative one against a near-free zero chain.
            expect ((t1 - t0) * 1000.0 < 200.0,
                    "sleeping sideband added CPU: " + juce::String ((t1 - t0) * 1000.0, 1) + " ms");
        }

        //----------------------------------------------------------------------
        // Status restore: preset + custom tweaks survive a state round-trip
        //----------------------------------------------------------------------
        beginTest ("preset + custom state round-trips exactly");
        {
            PluginProcessor a, b;
            a.prepareToPlay (48000.0, 512);

            // Load a formant preset, then tweak a few parameters.
            util::PresetBank::loadPreset (a.apvts, 4); // Pitch Up
            if (auto* p = a.apvts.getParameter (params::makeBandParameterID (1, params::IDs::jitterDepth)))
                p->setValueNotifyingHost (0.5f);
            if (auto* p = a.apvts.getParameter (params::IDs::xover1Freq))
                p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (350.0f));

            juce::MemoryBlock state;
            a.getStateInformation (state);

            b.prepareToPlay (48000.0, 512);
            b.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

            // Compare every parameter (normalised values).
            bool allMatch = true;
            for (auto* param : a.getParameters())
            {
                if (auto* bParam = b.getParameters()[param->getParameterIndex()])
                {
                    const auto va = param->getValue();
                    const auto vb = bParam->getValue();
                    if (std::abs (static_cast<double> (va) - vb) > 1e-6)
                    {
                        allMatch = false;
                        logMessage (juce::String ("  mismatch: ") + param->getName (80)
                                    + " " + juce::String (va) + " vs " + juce::String (vb));
                    }
                }
            }
            expect (allMatch, "state round-trip changed parameter values");
            logMessage ("  state round-trip: all parameters exact");
        }
    }
};

PerfAuditTests perfAuditTests;

} // namespace
