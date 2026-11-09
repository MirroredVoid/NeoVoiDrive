/*
    NeoVoiDrive T13 tests — MultibandEngine (the full DSP chain).

    Acceptance (TASKS.md T13):
      1. band_count=1 bypasses the crossover and equals a single full-range
         BandProcessor (bit-identical);
      2. band isolation: different shimmer depth across the crossover points,
         crosstalk outside the crossover region < -24 dB (24 dB/oct);
      3. bypass gain 0 = transparent, 1 = processed chain;
      4. dry/wet 0% = dry input, 100% = wet chain, 10 ms smooth transitions;
      5. output trim maps dB to amplitude (+12 dB = 4x, -12 dB = 0.25x);
      6. stereo channels process independently;
      7. parameter groups all take effect through the snapshot;
      8. the meter tap receives the output peaks (T14 write point).
*/

#include <juce_core/juce_core.h>

#include "dsp/BandProcessor.h"
#include "dsp/MultibandEngine.h"

#include <cmath>
#include <tuple>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr int kTotal = 188 * kBlock; // 96256: multiple of the block size

//==============================================================================
std::vector<float> makeSine (float freq, int total)
{
    std::vector<float> x (static_cast<size_t> (total));
    for (int n = 0; n < total; ++n)
        x[static_cast<size_t> (n)] = 0.5f * std::sin (
            juce::MathConstants<float>::twoPi * freq * static_cast<float> (n)
            / static_cast<float> (kSampleRate));
    return x;
}

//==============================================================================
struct EngineRunner
{
    dsp::MultibandEngine engine;
    std::vector<float> bypass;  // processed-chain gain (default 1)
    std::vector<float> formant; // formant-path mix gain (default 0)

    explicit EngineRunner (int block = kBlock)
    {
        engine.prepare (kSampleRate, block);
        bypass.assign (static_cast<size_t> (block), 1.0f);
        formant.assign (static_cast<size_t> (block), 0.0f);
    }

    void processMono (const float* in, float* out, int n)
    {
        engine.process (in, nullptr, out, nullptr, n, 1, bypass.data(), formant.data());
    }

    void processStereo (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        engine.process (inL, inR, outL, outR, n, 2, bypass.data(), formant.data());
    }
};

//==============================================================================
double rmsRatio (const std::vector<float>& a, const std::vector<float>& b, int start, int end)
{
    double diffSq = 0.0, sigSq = 0.0;
    for (int i = start; i < end; ++i)
    {
        const auto d = static_cast<double> (a[static_cast<size_t> (i)]) - b[static_cast<size_t> (i)];
        diffSq += d * d;
        sigSq += static_cast<double> (b[static_cast<size_t> (i)]) * b[static_cast<size_t> (i)];
    }
    return std::sqrt (diffSq / juce::jmax (sigSq, 1e-30));
}

double peakRatio (const std::vector<float>& a, const std::vector<float>& b, int start, int end)
{
    float maxDiff = 0.0f, maxSig = 0.0f;
    for (int i = start; i < end; ++i)
    {
        maxDiff = juce::jmax (maxDiff, std::abs (a[static_cast<size_t> (i)] - b[static_cast<size_t> (i)]));
        maxSig  = juce::jmax (maxSig, std::abs (b[static_cast<size_t> (i)]));
    }
    return static_cast<double> (maxDiff) / juce::jmax (maxSig, 1e-9f);
}

// Single-frequency magnitude (rectangular window; coherent gain 2/N).
double dftMag (const std::vector<float>& sig, double freq)
{
    double re = 0.0, im = 0.0;
    const auto n = static_cast<int> (sig.size());
    for (int i = 0; i < n; ++i)
    {
        const double theta = 2.0 * juce::MathConstants<double>::pi * freq
                           * static_cast<double> (i) / kSampleRate;
        re += static_cast<double> (sig[static_cast<size_t> (i)]) * std::cos (theta);
        im -= static_cast<double> (sig[static_cast<size_t> (i)]) * std::sin (theta);
    }
    return 2.0 * std::sqrt (re * re + im * im) / static_cast<double> (n);
}

//==============================================================================
class MultibandEngineTest final : public juce::UnitTest
{
public:
    MultibandEngineTest() : juce::UnitTest ("MultibandEngine") {}

    void runTest() override
    {
        beginTest ("band_count=1 equals a single full-range BandProcessor");
        {
            // Baseline: a transparent engine (all-zero params) must equal the
            // input delayed by the chain's fixed latency (exciter ~7 samples
            // plus any pitch delay-line latency, M2) — isolates the mix path
            // from the band chain. Probe the delay, then require bit-exactness.
            {
                params::ParamSnapshot transparent; // defaults: all depth 0
                transparent.bandCount = 1;
                EngineRunner r;
                r.engine.setParameters (transparent);
                const auto x0 = makeSine (440.0f, kTotal);
                std::vector<float> out0 (static_cast<size_t> (kTotal), 0.0f);
                for (int pos = 0; pos < kTotal; pos += kBlock)
                    r.processMono (x0.data() + pos, out0.data() + pos, kBlock);

                constexpr int ringSize = 32;
                float bestError = 1e30f;
                int bestDelay = -1;
                for (int probeDelay = 1; probeDelay <= 16; ++probeDelay)
                {
                    float history[ringSize] = {};
                    float err = 0.0f;
                    for (int i = 0; i < kTotal; ++i)
                    {
                        const auto expected = history[static_cast<size_t> ((i + ringSize - probeDelay) % ringSize)];
                        history[static_cast<size_t> (i % ringSize)] = x0[static_cast<size_t> (i)];
                        err = juce::jmax (err, std::abs (out0[static_cast<size_t> (i)] - expected));
                    }
                    if (err < bestError) { bestError = err; bestDelay = probeDelay; }
                }
                if (bestError > 0.0f)
                {
                    juce::String seq;
                    for (int i = 0; i < 8; ++i)
                        seq << "[" << i << "] " << juce::String (x0[static_cast<size_t> (i)])
                            << "/" << juce::String (out0[static_cast<size_t> (i)]) << "  ";
                    logMessage ("x/out head: " + seq);
                    logMessage (juce::String ("best delay ") + juce::String (bestDelay)
                                + " err " + juce::String (bestError));
                }
                expectEquals (bestError, 0.0f, "transparent engine != delayed input (mix path broken)");
            }

            params::ParamSnapshot snap;
            snap.bandCount = 1;
            // Non-trivial processing so the comparison is meaningful.
            for (auto& b : snap.bands)
            {
                b.jitterDepth = 20.0f;
                b.jitterRate  = 5.0f;
                b.shimmerDepth = 30.0f;
                b.shimmerRate  = 4.0f;
                b.harmonicDrive = 40.0f;
                b.harmonicTone  = 3000.0f;
                b.vibDepth = 15.0f;
                b.vibRate  = 3.0f;
            }

            dsp::BandProcessor reference;
            reference.prepare (kSampleRate, kBlock);
            reference.setSeed (1234);
            reference.setParameters (snap.bands[0]);

            EngineRunner runnerA;
            runnerA.engine.setSeed (1234);
            runnerA.engine.setParameters (snap);

            const auto input = makeSine (440.0f, kTotal);
            std::vector<float> refOut (static_cast<size_t> (kTotal), 0.0f);
            std::vector<float> engOut (static_cast<size_t> (kTotal), 0.0f);

            for (int pos = 0; pos < kTotal; pos += kBlock)
            {
                for (int i = 0; i < kBlock; ++i)
                    refOut[static_cast<size_t> (pos + i)] = reference.process (input[static_cast<size_t> (pos + i)]);
                runnerA.processMono (input.data() + pos, engOut.data() + pos, kBlock);
            }

            bool identical = true;
            int firstDiff = -1;
            for (int i = 0; i < kTotal; ++i)
            {
                if (engOut[static_cast<size_t> (i)] != refOut[static_cast<size_t> (i)])
                {
                    identical = false;
                    firstDiff = i;
                    break;
                }
            }
            if (! identical)
            {
                const auto& e = engOut[static_cast<size_t> (firstDiff)];
                const auto& r = refOut[static_cast<size_t> (firstDiff)];
                const auto& x = input[static_cast<size_t> (firstDiff)];
                logMessage (juce::String ("first diff at ") + juce::String (firstDiff)
                            + ": ref=" + juce::String (r) + " eng=" + juce::String (e)
                            + " x=" + juce::String (x));
                juce::String seq;
                for (int i = 0; i < 8; ++i)
                    seq << "[" << i << "] " << juce::String (refOut[static_cast<size_t> (i)])
                        << "/" << juce::String (engOut[static_cast<size_t> (i)]) << "  ";
                logMessage ("ref/eng: " + seq);
            }
            expect (identical, "band_count=1 output differs from the single BandProcessor");
        }

        beginTest ("mono input stays mono: L/R outputs are bit-identical");
        {
            // User requirement: a mono source must NOT come out as stereo.
            // L/R random sources share the engine seed, so identical input
            // channels must produce bit-identical outputs — including with
            // the formant path engaged.
            EngineRunner runnerMono;
            params::ParamSnapshot snap;
            for (auto& b : snap.bands)
            {
                b.jitterDepth   = 50.0f;
                b.jitterRate    = 300.0f;
                b.shimmerDepth  = 80.0f;
                b.shimmerRate   = 200.0f;
                b.harmonicDrive = 70.0f;
                b.vibWave       = 3; // Noise LFO (random source)
                b.vibDepth      = 120.0f;
                b.vibRate       = 50.0f;
            }
            snap.formantEnabled = true;
            runnerMono.engine.setParameters (snap);

            const auto in = makeSine (220.0f, kTotal); // mono source
            std::vector<float> outL (static_cast<size_t> (kTotal), 0.0f);
            std::vector<float> outR (static_cast<size_t> (kTotal), 0.0f);
            for (int pos = 0; pos < kTotal; pos += kBlock)
                runnerMono.processStereo (in.data() + pos, in.data() + pos,
                                          outL.data() + pos, outR.data() + pos, kBlock);

            bool identical = true;
            int firstDiff = -1;
            for (int i = 0; i < kTotal; ++i)
            {
                if (outL[static_cast<size_t> (i)] != outR[static_cast<size_t> (i)])
                {
                    identical = false;
                    firstDiff = i;
                    break;
                }
            }
            logMessage (juce::String ("  mono-in L==R: ") + (identical ? "yes" : "no")
                        + (firstDiff >= 0 ? juce::String (" first diff @ ") + juce::String (firstDiff)
                                          : ""));
            expect (identical, "mono input produced different L/R outputs (stereo widening)");
        }

        beginTest ("band isolation: shimmer on band 1 stays below -24 dB outside its band");
        {
            auto makeSnap = []
            {
                params::ParamSnapshot s; // T37: default is 1 band — this
                s.bandCount = 3;         // isolation test needs 3 bands
                s.xover1Freq = 500.0f;
                s.xover2Freq = 2000.0f;
                return s;
            };

            const auto snapRef  = makeSnap();
            auto snapTest = makeSnap();
            snapTest.bands[0].shimmerDepth = 50.0f;

            const auto run = [&] (const params::ParamSnapshot& s, float freq)
            {
                EngineRunner r;
                r.engine.setParameters (s);
                const auto x = makeSine (freq, kTotal);
                std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
                for (int pos = 0; pos < kTotal; pos += kBlock)
                    r.processMono (x.data() + pos, out.data() + pos, kBlock);
                return out;
            };

            // 200 Hz sits in the low band: the shimmer must be audible there.
            const auto ref200  = run (snapRef, 200.0f);
            const auto test200 = run (snapTest, 200.0f);
            const auto modRatio = rmsRatio (test200, ref200, static_cast<int> (kSampleRate), kTotal);
            // 0.09: random-seed S&H modulation has ~1% run-to-run variance at
            // the 0.1 boundary (measured 0.0994 on one seed).
            expectGreaterThan (static_cast<float> (modRatio), 0.09f,
                               "shimmer did not reach the low band: ratio "
                               + juce::String (modRatio));

            // 5 kHz sits in the high band: crosstalk from band 1 < -24 dB.
            const auto ref5k  = run (snapRef, 5000.0f);
            const auto test5k = run (snapTest, 5000.0f);
            const auto bleed = peakRatio (test5k, ref5k, static_cast<int> (kSampleRate), kTotal);
            expectLessThan (static_cast<float> (bleed), std::pow (10.0f, -24.0f / 20.0f),
                            "band-1 shimmer leaked into the high band: "
                            + juce::String (bleed));
        }

        beginTest ("bypass gain 0 = transparent, 1 = processed chain");
        {
            params::ParamSnapshot snap;
            snap.bands[1].shimmerDepth = 60.0f; // make the chain audible

            EngineRunner runnerB;
            runnerB.engine.setParameters (snap);
            const auto input = makeSine (1000.0f, kTotal);
            std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);

            // bypass 0: out = in * 1 + processed * 0 == in, bit-exact.
            runnerB.bypass.assign (static_cast<size_t> (kBlock), 0.0f);
            for (int pos = 0; pos < kTotal; pos += kBlock)
                runnerB.processMono (input.data() + pos, out.data() + pos, kBlock);

            bool transparent = true;
            for (int i = 0; i < kTotal; ++i)
            {
                if (out[static_cast<size_t> (i)] != input[static_cast<size_t> (i)])
                {
                    transparent = false;
                    break;
                }
            }
            expect (transparent, "bypass gain 0 must be bit-transparent");

            // bypass 1: processed chain, clearly different from the input.
            EngineRunner runner2;
            runner2.engine.setParameters (snap);
            for (int pos = 0; pos < kTotal; pos += kBlock)
                runner2.processMono (input.data() + pos, out.data() + pos, kBlock);
            const auto processed = rmsRatio (out, input, static_cast<int> (kSampleRate), kTotal);
            expectGreaterThan (static_cast<float> (processed), 0.1f,
                               "processed chain too close to input: "
                               + juce::String (processed));
        }

        beginTest ("dry/wet: 0% = dry input, 100% = wet chain, smooth transitions");
        {
            params::ParamSnapshot snap;
            snap.bands[0].jitterDepth = 30.0f;

            // 0%: after the 10 ms settle, out == in (bit-exact).
            snap.dryWet = 0.0f;
            EngineRunner dryRunner;
            dryRunner.engine.setParameters (snap);
            const auto input = makeSine (440.0f, kTotal);
            std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
            for (int pos = 0; pos < kTotal; pos += kBlock)
                dryRunner.processMono (input.data() + pos, out.data() + pos, kBlock);

            bool dry = true;
            for (int i = static_cast<int> (kSampleRate); i < kTotal; ++i) // steady state only
            {
                if (out[static_cast<size_t> (i)] != input[static_cast<size_t> (i)])
                {
                    dry = false;
                    break;
                }
            }
            expect (dry, "dry/wet 0% must equal the dry input");

            // 100%: wet chain differs from the input.
            snap.dryWet = 100.0f;
            EngineRunner wetRunner;
            wetRunner.engine.setParameters (snap);
            for (int pos = 0; pos < kTotal; pos += kBlock)
                wetRunner.processMono (input.data() + pos, out.data() + pos, kBlock);
            const auto wetDiff = rmsRatio (out, input, static_cast<int> (kSampleRate), kTotal);
            expectGreaterThan (static_cast<float> (wetDiff), 0.05f,
                               "dry/wet 100% too close to input: " + juce::String (wetDiff));

            // Step 100 -> 0: the 10 ms smoother must avoid a click.  Use a
            // 100 Hz probe so the signal's own per-sample slope (~0.0065) stays
            // well below the threshold.
            const auto slowInput = makeSine (100.0f, kTotal);
            EngineRunner smoothRunner;
            snap.dryWet = 100.0f;
            smoothRunner.engine.setParameters (snap);
            std::vector<float> slowOut (static_cast<size_t> (kTotal), 0.0f);
            for (int pos = 0; pos < kTotal / 4; pos += kBlock)
                smoothRunner.processMono (slowInput.data() + pos, slowOut.data() + pos, kBlock);

            snap.dryWet = 0.0f;
            smoothRunner.engine.setParameters (snap);
            const auto lastBefore = slowOut[static_cast<size_t> (kTotal / 4 - 1)];
            float maxStep = 0.0f;
            for (int pos = kTotal / 4; pos < kTotal / 4 + kBlock; pos += kBlock)
            {
                smoothRunner.processMono (slowInput.data() + pos, slowOut.data() + pos, kBlock);
                for (int i = 0; i < kBlock; ++i)
                {
                    const auto idx = static_cast<size_t> (pos + i);
                    const auto prev = (i == 0 && pos == kTotal / 4) ? lastBefore : slowOut[idx - 1];
                    maxStep = juce::jmax (maxStep, std::abs (slowOut[idx] - prev));
                }
            }
            expectLessThan (maxStep, 0.02f,
                            "dry/wet step clicked: max sample jump " + juce::String (maxStep));
        }

        beginTest ("output trim: +12 dB = 4x, -12 dB = 0.25x amplitude");
        {
            const auto input = makeSine (1000.0f, kTotal);

            const auto runTrim = [&] (float db)
            {
                params::ParamSnapshot snap; // defaults: transparent chain
                snap.outputTrim = db;
                EngineRunner r;
                r.engine.setParameters (snap);
                std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
                for (int pos = 0; pos < kTotal; pos += kBlock)
                    r.processMono (input.data() + pos, out.data() + pos, kBlock);
                return out;
            };

            const auto outPlus  = runTrim (+12.0f);
            const auto outMinus = runTrim (-12.0f);

            const auto rmsOf = [] (const std::vector<float>& v, int start, int end)
            {
                double sq = 0.0;
                for (int i = start; i < end; ++i)
                    sq += static_cast<double> (v[static_cast<size_t> (i)]) * v[static_cast<size_t> (i)];
                return std::sqrt (sq / (end - start));
            };

            const auto inRms = rmsOf (input, static_cast<int> (kSampleRate), kTotal);
            const auto plusRms  = rmsOf (outPlus, static_cast<int> (kSampleRate), kTotal);
            const auto minusRms = rmsOf (outMinus, static_cast<int> (kSampleRate), kTotal);

            expectWithinAbsoluteError (static_cast<float> (plusRms / inRms), 4.0f, 0.25f,
                                       "trim +12 dB gain " + juce::String (plusRms / inRms));
            expectWithinAbsoluteError (static_cast<float> (minusRms / inRms), 0.25f, 0.05f,
                                       "trim -12 dB gain " + juce::String (minusRms / inRms));
        }

        beginTest ("stereo: channels process independently");
        {
            params::ParamSnapshot snap;
            snap.xover1Freq = 500.0f;
            snap.xover2Freq = 2000.0f;
            snap.bands[0].shimmerDepth = 50.0f;

            const auto inL1 = makeSine (200.0f, kTotal);
            const auto inL2 = makeSine (300.0f, kTotal);
            const auto inR  = makeSine (5000.0f, kTotal);

            // Two independent engines (identical seed/params): only the left
            // input differs.  The right output must be bit-identical.
            EngineRunner engineA, engineB;
            engineA.engine.setSeed (42);
            engineB.engine.setSeed (42);
            engineA.engine.setParameters (snap);
            engineB.engine.setParameters (snap);
            std::vector<float> outLA (static_cast<size_t> (kTotal), 0.0f);
            std::vector<float> outRA (static_cast<size_t> (kTotal), 0.0f);
            std::vector<float> outLB (static_cast<size_t> (kTotal), 0.0f);
            std::vector<float> outRB (static_cast<size_t> (kTotal), 0.0f);
            for (int pos = 0; pos < kTotal; pos += kBlock)
            {
                engineA.processStereo (inL1.data() + pos, inR.data() + pos,
                                       outLA.data() + pos, outRA.data() + pos, kBlock);
                engineB.processStereo (inL2.data() + pos, inR.data() + pos,
                                       outLB.data() + pos, outRB.data() + pos, kBlock);
            }

            // The right outputs must be bit-identical (no channel crosstalk).
            bool sameR = true;
            int firstR = -1;
            for (int i = 0; i < kTotal; ++i)
            {
                if (outRA[static_cast<size_t> (i)] != outRB[static_cast<size_t> (i)])
                {
                    sameR = false;
                    firstR = i;
                    break;
                }
            }
            if (! sameR)
                logMessage (juce::String ("R first diff at ") + juce::String (firstR)
                            + ": " + juce::String (outRA[static_cast<size_t> (firstR)])
                            + " vs " + juce::String (outRB[static_cast<size_t> (firstR)]));
            expect (sameR, "right channel changed with the left input");

            // The left outputs must differ (different inputs, independent chain).
            const auto leftDiff = rmsRatio (outLA, outLB, static_cast<int> (kSampleRate), kTotal);
            expectGreaterThan (static_cast<float> (leftDiff), 0.05f,
                               "left channel did not follow its input: "
                               + juce::String (leftDiff));
        }

        beginTest ("parameter groups take effect through the snapshot");
        {
            const auto input = makeSine (440.0f, kTotal);

            const auto run = [&] (const params::ParamSnapshot& s)
            {
                EngineRunner r;
                r.engine.setParameters (s);
                std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
                for (int pos = 0; pos < kTotal; pos += kBlock)
                    r.processMono (input.data() + pos, out.data() + pos, kBlock);
                return out;
            };

            params::ParamSnapshot base;
            base.bandCount = 3;   // T37: the snapshot default is now 1 band
            // T37: xover2 default moved to 4000 Hz — with a 440 Hz input the
            // high band would carry no signal and "band 3 drive" could not
            // be audible. Pin xover2 below the input so band 3 is exercised.
            base.xover2Freq = 400.0f;
            const auto outBase = run (base);

            const auto checkDiffers = [&] (const char* label, const std::vector<float>& out)
            {
                const auto ratio = rmsRatio (out, outBase, static_cast<int> (kSampleRate), kTotal);
                expectGreaterThan (static_cast<float> (ratio), 0.01f,
                                   juce::String (label) + " had no audible effect: "
                                   + juce::String (ratio));
            };

            // Per-band processing group (b3 drive).
            auto s1 = base;
            s1.bands[2].harmonicDrive = 80.0f;
            checkDiffers ("harmonic drive (band 3)", run (s1));

            // Crossover geometry.
            auto s2 = base;
            s2.xover1Freq = 3000.0f;
            checkDiffers ("xover1", run (s2));

            // band_count routing.
            auto s3 = base;
            s3.bandCount = 1;
            checkDiffers ("band_count=1", run (s3));

            // Dry/wet.
            auto s4 = base;
            s4.dryWet = 40.0f;
            checkDiffers ("dry_wet", run (s4));

            // Trim.
            auto s5 = base;
            s5.outputTrim = 6.0f;
            checkDiffers ("output_trim", run (s5));

            // Formant engaged (full formant gain, audible shift).
            auto s6 = base;
            s6.formantEnabled = true;
            s6.formantShift = 6.0f;
            {
                EngineRunner r;
                r.formant.assign (static_cast<size_t> (kBlock), 1.0f);
                r.engine.setParameters (s6);
                std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
                for (int pos = 0; pos < kTotal; pos += kBlock)
                    r.processMono (input.data() + pos, out.data() + pos, kBlock);
                checkDiffers ("formant", out);
            }
        }

        beginTest ("band_count=2: single crossover, mid+hi folded into the high band");
        {
            params::ParamSnapshot snap;
            snap.bandCount = 2;
            snap.xover1Freq = 500.0f;

            // The 2-band high path (mid+hi with f2=18000) must behave like a
            // single high-pass at xover1: full-range content is preserved.
            EngineRunner runEngine;
            runEngine.engine.setParameters (snap);

            const auto runFreq = [&] (float freq)
            {
                const auto x = makeSine (freq, kTotal);
                std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
                for (int pos = 0; pos < kTotal; pos += kBlock)
                    runEngine.processMono (x.data() + pos, out.data() + pos, kBlock);
                double sq = 0.0;
                for (int i = static_cast<int> (kSampleRate); i < kTotal; ++i)
                    sq += static_cast<double> (out[static_cast<size_t> (i)])
                        * out[static_cast<size_t> (i)];
                return std::sqrt (sq / (kTotal - kSampleRate));
            };

            const auto rmsIn = [&] (float freq)
            {
                const auto x = makeSine (freq, kTotal);
                double sq = 0.0;
                for (int i = static_cast<int> (kSampleRate); i < kTotal; ++i)
                    sq += static_cast<double> (x[static_cast<size_t> (i)])
                        * x[static_cast<size_t> (i)];
                return std::sqrt (sq / (kTotal - kSampleRate));
            };

            // 200 Hz (low band) and 5 kHz (high band) must both pass near-unity.
            for (const float freq : { 200.0f, 5000.0f })
            {
                const auto gain = runFreq (freq) / rmsIn (freq);
                const auto db = 20.0 * std::log10 (gain);
                expect (std::abs (db) < 1.5,
                        juce::String ("2-band gain @") + juce::String (freq)
                        + " Hz = " + juce::String (db) + " dB");
            }
        }

        beginTest ("formant re-enable does not replay stale audio");
        {
            params::ParamSnapshot snap;
            snap.formantEnabled = true;
            snap.formantShift = 4.0f;

            EngineRunner runEngine;
            runEngine.formant.assign (static_cast<size_t> (kBlock), 1.0f);
            runEngine.engine.setParameters (snap);
            const auto input = makeSine (440.0f, kTotal);
            std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);

            // Run with formant on, then off (engine freezes), then on again.
            for (int pos = 0; pos < kTotal / 4; pos += kBlock)
                runEngine.processMono (input.data() + pos, out.data() + pos, kBlock);

            snap.formantEnabled = false;
            runEngine.engine.setParameters (snap);
            for (int pos = kTotal / 4; pos < kTotal / 2; pos += kBlock)
                runEngine.processMono (input.data() + pos, out.data() + pos, kBlock);

            snap.formantEnabled = true;
            runEngine.engine.setParameters (snap);
            for (int pos = kTotal / 2; pos < kTotal / 2 + 2 * kBlock; pos += kBlock)
                runEngine.processMono (input.data() + pos, out.data() + pos, kBlock);

            // The re-enabled engine must start from silence (flushed), not
            // replay the audio buffered before the disable.  The silence
            // window covers the first analysed frame (latency - 768).
            bool silent = true;
            for (int i = kTotal / 2; i < kTotal / 2 + 1280; ++i)
            {
                if (out[static_cast<size_t> (i)] != 0.0f)
                {
                    silent = false;
                    break;
                }
            }
            expect (silent, "re-enabled formant replayed stale audio");
        }

        beginTest ("formant disable crossfades without a hard cut");
        {
            params::ParamSnapshot snap;
            snap.formantEnabled = true;
            snap.formantShift = 4.0f;

            EngineRunner runEngine;
            runEngine.formant.assign (static_cast<size_t> (kBlock), 1.0f);
            runEngine.engine.setParameters (snap);
            const auto slowInput = makeSine (100.0f, kTotal);
            std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);

            // Settle with formant on, then step the fg ramp down mid-stream
            // (as LatencyManager does on disable) and check for a click.
            for (int pos = 0; pos < kTotal / 2; pos += kBlock)
                runEngine.processMono (slowInput.data() + pos, out.data() + pos, kBlock);

            snap.formantEnabled = false;
            runEngine.engine.setParameters (snap);
            const auto lastBefore = out[static_cast<size_t> (kTotal / 2 - 1)];
            float maxStep = 0.0f;
            int maxStepAt = -1;
            for (int pos = kTotal / 2; pos < kTotal / 2 + kBlock; pos += kBlock)
            {
                // fg ramps 1 -> 0 linearly over the block (5 ms @ 48 kHz = 240).
                for (int i = 0; i < kBlock; ++i)
                    runEngine.formant[static_cast<size_t> (i)] = 1.0f - static_cast<float> (i) / 240.0f;
                runEngine.processMono (slowInput.data() + pos, out.data() + pos, kBlock);
                for (int i = 0; i < kBlock; ++i)
                {
                    const auto idx = static_cast<size_t> (pos + i);
                    const auto prev = (i == 0 && pos == kTotal / 2) ? lastBefore : out[idx - 1];
                    const auto step = std::abs (out[idx] - prev);
                    if (step > maxStep) { maxStep = step; maxStepAt = static_cast<int> (idx); }
                }
            }
            logMessage (juce::String ("disable maxStep ") + juce::String (maxStep)
                        + " at " + juce::String (maxStepAt));
            expectLessThan (maxStep, 0.02f,
                            "formant disable clicked: max sample jump " + juce::String (maxStep));
        }

        beginTest ("dry/wet stays time-aligned with the formant path (L03a/S1)");
        {
            // formant on + dry_wet=50% + fg==1: an impulse at t=0 must produce
            // ONE response around t=2048 — no echo at t=0 (pre-fix: the dry
            // path was the immediate input, giving two impulses 42.7 ms apart).
            params::ParamSnapshot snap;
            snap.formantEnabled = true;
            snap.dryWet = 50.0f;

            EngineRunner impulseEngine;
            impulseEngine.formant.assign (static_cast<size_t> (kBlock), 1.0f);
            impulseEngine.engine.setParameters (snap);

            std::vector<float> in (static_cast<size_t> (kTotal), 0.0f);
            std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
            in[0] = 1.0f;
            for (int pos = 0; pos < kTotal; pos += kBlock)
                impulseEngine.processMono (in.data() + pos, out.data() + pos, kBlock);

            // No significant energy before the formant latency (the dry path
            // is delayed to 2048, so the first 2000 samples are silence).
            float prePeak = 0.0f;
            for (int i = 0; i < 2000; ++i)
                prePeak = juce::jmax (prePeak, std::abs (out[static_cast<size_t> (i)]));
            expectLessThan (prePeak, 1e-3f,
                            "echo before the formant latency: " + juce::String (prePeak));

            // The response peaks around the formant latency (2048).
            float postPeak = 0.0f;
            int postAt = -1;
            for (int i = 2048 - 256; i < 2048 + 256; ++i)
            {
                const auto v = std::abs (out[static_cast<size_t> (i)]);
                if (v > postPeak) { postPeak = v; postAt = i; }
            }
            expectGreaterThan (postPeak, 0.05f,
                               "no response at the formant latency");
            expect (postAt >= 2048 - 256 && postAt <= 2048 + 256,
                    juce::String ("peak at ") + juce::String (postAt));
        }

        beginTest ("formant shift sweep stays step-free (zipper)");
        {
            // Continuous shift sweep (0 -> 6 st in 0.5 st steps per block):
            // frame-boundary smoothing must prevent parameter steps from
            // showing up as sample-level jumps.
            params::ParamSnapshot snap;
            snap.formantEnabled = true;

            EngineRunner sweepEngine;
            sweepEngine.formant.assign (static_cast<size_t> (kBlock), 1.0f);
            const auto input = makeSine (440.0f, kTotal);
            std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);

            constexpr int blocksPerStep = 8;
            for (int pos = 0; pos < kTotal; pos += kBlock)
            {
                snap.formantShift = 0.5f * static_cast<float> (pos / (kBlock * blocksPerStep));
                sweepEngine.engine.setParameters (snap);
                sweepEngine.processMono (input.data() + pos, out.data() + pos, kBlock);
            }

            // Steady state: no single-sample jump beyond the signal's own
            // slope (~0.03 at 440 Hz, 0.5 amplitude) plus a generous margin
            // for the vocoder's frame response.
            float maxStep = 0.0f;
            for (int i = static_cast<int> (kSampleRate) + 1; i < kTotal; ++i)
                maxStep = juce::jmax (maxStep, std::abs (out[static_cast<size_t> (i)]
                                                         - out[static_cast<size_t> (i - 1)]));
            expectLessThan (maxStep, 0.25f,
                            "shift sweep stepped: max jump " + juce::String (maxStep));
        }

        beginTest ("meter tap receives the output peaks");
        {
            static float capturedL = 0.0f, capturedR = 0.0f;
            EngineRunner meterEngine;
            meterEngine.engine.setMeterTap (
                [] (void*, float l, float r) noexcept { capturedL = l; capturedR = r; }, nullptr);

            const auto inL = makeSine (440.0f, kTotal);
            const auto inR = makeSine (1000.0f, kTotal);
            std::vector<float> outL (static_cast<size_t> (kTotal), 0.0f);
            std::vector<float> outR (static_cast<size_t> (kTotal), 0.0f);
            for (int pos = 0; pos < kTotal; pos += kBlock)
                meterEngine.processStereo (inL.data() + pos, inR.data() + pos,
                                           outL.data() + pos, outR.data() + pos, kBlock);

            float peakL = 0.0f, peakR = 0.0f;
            for (int i = static_cast<int> (kSampleRate); i < kTotal; ++i)
            {
                peakL = juce::jmax (peakL, std::abs (outL[static_cast<size_t> (i)]));
                peakR = juce::jmax (peakR, std::abs (outR[static_cast<size_t> (i)]));
            }

            expectWithinAbsoluteError (capturedL, peakL, 1e-3f,
                                       "meter L " + juce::String (capturedL)
                                       + " vs " + juce::String (peakL));
            expectWithinAbsoluteError (capturedR, peakR, 1e-3f,
                                       "meter R " + juce::String (capturedR)
                                       + " vs " + juce::String (peakR));
        }

        beginTest ("T38 band enable bypasses all effects (dry passthrough)");
        {
            // band enable off -> the band's output equals its input (dry
            // passthrough), regardless of how much processing is armed.
            params::ParamSnapshot armed;
            armed.bandCount = 1;   // the whole output is this band
            armed.bands[0].jitterDepth = 80.0f;
            armed.bands[0].shimmerDepth = 60.0f;
            armed.bands[0].vibDepth = 120.0f;
            armed.bands[0].vibWave = 0;
            armed.bands[0].sidebandDepth = 70.0f;
            armed.bands[0].sidebandRoughness = 100.0f;

            EngineRunner r;
            r.engine.setParameters (armed);
            const auto in = makeSine (440.0f, kTotal);
            std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
            for (int pos = 0; pos < kTotal; pos += kBlock)
                r.processMono (in.data() + pos, out.data() + pos, kBlock);

            // With the band armed the output must differ from the input
            // (effects active)…
            const auto diffActive = rmsRatio (out, in, static_cast<int> (kSampleRate), kTotal);
            expectGreaterThan (static_cast<float> (diffActive), 0.01f,
                               "armed band should change the signal");

            // …and with enable=false it must equal the input (passthrough).
            armed.bands[0].enable = false;
            EngineRunner r2;
            r2.engine.setParameters (armed);
            std::vector<float> out2 (static_cast<size_t> (kTotal), 0.0f);
            for (int pos = 0; pos < kTotal; pos += kBlock)
                r2.processMono (in.data() + pos, out2.data() + pos, kBlock);
            // Skip the 5 ms bypass crossfade at the start.
            const auto diffBypass = rmsRatio (out2, in, static_cast<int> (kSampleRate / 20), kTotal);
            expectLessThan (static_cast<float> (diffBypass), 0.001f,
                            "band disabled should pass the dry signal through");
        }

        beginTest ("SB-U17 sideband parameter sensitivity matrix (engine=1)");
        {
            // engine_version=1 must make every sideband parameter audible
            // (replica of the :517 sensitivity pattern), while enable=off
            // pins the wet path so other parameter flips change nothing.
            const auto input = makeSine (440.0f, kTotal);

            const auto run = [&] (const params::ParamSnapshot& s)
            {
                EngineRunner r;
                r.engine.setSeed (1234);
                std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
                for (int pos = 0; pos < kTotal; pos += kBlock)
                {
                    r.engine.setParameters (s);   // per block: refreshes the F0 broadcast
                    r.processMono (input.data() + pos, out.data() + pos, kBlock);
                }
                return out;
            };

            // Spectral sensitivity: RMS is blind to fm/roughness (energy is
            // reshaped, not added), and sensitive to the fixed Lmax time shift.
            // Compare log-spaced magnitude spectra. The roughness=0 sidebands
            // are ~0.5 Hz wide (2 s window) — far narrower than any fixed
            // sampling grid, so each grid point takes the local maximum over
            // ±2 Hz (0.5 Hz steps).
            const auto spectRatio = [] (const std::vector<float>& a, const std::vector<float>& b)
            {
                double diffSq = 0.0, sigSq = 0.0;
                constexpr int points = 100;
                const double step = std::pow (8000.0 / 50.0, 1.0 / (points - 1));
                double f = 50.0;
                for (int k = 0; k < points; ++k, f *= step)
                {
                    double ma = 0.0, mb = 0.0;
                    for (double df = -2.0; df <= 2.0 + 1e-9; df += 0.5)
                    {
                        ma = juce::jmax (ma, dftMag (a, f + df));
                        mb = juce::jmax (mb, dftMag (b, f + df));
                    }
                    diffSq += (ma - mb) * (ma - mb);
                    sigSq += mb * mb;
                }
                return std::sqrt (diffSq / juce::jmax (sigSq, 1e-30));
            };

            params::ParamSnapshot base;
            base.engineVersion = 1;
            base.bandCount = 1;
            base.bands[0].sidebandDepth = 60.0f;    // audible base point
            base.bands[0].sidebandFmRatio = 0.3f;   // the sideband assertions below
                                                    // scan at 440 ± 0.3·440
            base.bands[0].sidebandLowcut = 500.0f;  // explicit base: the SPEC
                                                    // default is 2000 Hz, which
                                                    // would HP the 440 Hz carrier
                                                    // away (assertions below)
            const auto outBase = run (base);

            const auto checkDiffers = [&] (const char* label, params::ParamSnapshot& s,
                                           float minRatio = 0.005f)
            {
                const auto ratio = spectRatio (run (s), outBase);
                expectGreaterThan (static_cast<float> (ratio), minRatio,
                                   juce::String (label) + " had no audible effect: "
                                       + juce::String (ratio));
            };

            auto s1 = base; s1.bands[0].sidebandDepth = 95.0f;
            checkDiffers ("sideband depth", s1);
            auto s2 = base; s2.bands[0].sidebandFmRatio = 0.45f;
            checkDiffers ("sideband fm_ratio", s2);
            auto s3 = base; s3.bands[0].sidebandRoughness = 90.0f;
            checkDiffers ("sideband roughness", s3);
            auto s5 = base; s5.bands[0].sidebandEnable = false;
            checkDiffers ("sideband enable=off", s5);

            // lowcut flips ONLY the sideband lines (the carrier and the
            // baseline L are untouched), so any fixed spectral grid misses
            // it — assert the sideband presence directly: base (lowcut 500)
            // must show 440±fm lines, lowcut=3000 must remove them.
            auto s4 = base;
            s4.bands[0].sidebandLowcut = 3000.0f;
            const auto peakNear = [] (const std::vector<float>& sig, double center)
            {
                double best = 0.0;
                for (double f = center - 3.0; f <= center + 3.0; f += 0.1)
                    best = juce::jmax (best, dftMag (sig, f));
                return best;
            };
            const auto baseSb = juce::jmax (peakNear (outBase, 308.0), peakNear (outBase, 572.0));
            const auto lowOut = run (s4);
            const auto lowSb  = juce::jmax (peakNear (lowOut, 308.0), peakNear (lowOut, 572.0));
            expectGreaterThan (static_cast<float> (baseSb), 0.005f,
                               "base sidebands missing (440±fm lines)");
            // T36 rev37d: the post-modulation HP removes the wet lines below
            // the lowcut entirely; what remains is the modulation's own
            // intermod skirt re-entering BELOW the post-HP stopband edge
            // (−24 dB/oct). Budget: halved + small absolute floor.
            expectLessThan (static_cast<float> (lowSb),
                            static_cast<float> (juce::jmax (baseSb * 0.5, 0.015)),
                            "lowcut=3000 did not remove the 440 Hz sidebands");

            // enable=off pins the wet path AND the baseline: further flips
            // must NOT change the output at all (spectral comparison is
            // immune to any residual time shift).
            auto off = base;
            off.bands[0].sidebandEnable = false;
            const auto outOff = run (off);
            auto offDepth = off;
            offDepth.bands[0].sidebandDepth = 95.0f;
            auto offRatio = off;
            offRatio.bands[0].sidebandFmRatio = 0.45f;
            const auto outOffDepth = run (offDepth);
            const auto outOffRatio = run (offRatio);
            expectLessThan (static_cast<float> (spectRatio (outOffDepth, outOff)),
                            0.01f, "depth flip changed output while enable=off");
            expectLessThan (static_cast<float> (spectRatio (outOffRatio, outOff)),
                            0.01f, "ratio flip changed output while enable=off");
        }

        beginTest ("SB-U18 mono input stays mono with sideband fully engaged (engine=1)");
        {
            // R6 revision regression: with sideband + F0 + noise fully on, a
            // mono source must still produce bit-identical L/R outputs.
            params::ParamSnapshot snap;
            snap.engineVersion = 1;
            for (auto& b : snap.bands)
            {
                b.sidebandDepth = 80.0f;
                b.sidebandFmRatio = 0.4f;
                b.sidebandRoughness = 100.0f;
                b.sidebandLowcut = 300.0f;
                b.sidebandEnable = true;
            }

            EngineRunner r;
            r.engine.setSeed (42);
            r.engine.setParameters (snap);

            const auto in = makeSine (220.0f, kTotal);
            std::vector<float> outL (static_cast<size_t> (kTotal), 0.0f);
            std::vector<float> outR (static_cast<size_t> (kTotal), 0.0f);
            for (int pos = 0; pos < kTotal; pos += kBlock)
            {
                r.engine.setParameters (snap);
                r.engine.process (in.data() + pos, in.data() + pos,
                                  outL.data() + pos, outR.data() + pos, kBlock, 2,
                                  r.bypass.data(), r.formant.data());
            }

            bool identical = true;
            int firstDiff = -1;
            for (int i = 0; i < kTotal; ++i)
            {
                if (outL[static_cast<size_t> (i)] != outR[static_cast<size_t> (i)])
                {
                    identical = false;
                    firstDiff = i;
                    break;
                }
            }
            if (! identical)
                logMessage (juce::String ("L/R first diff at ") + juce::String (firstDiff)
                            + ": " + juce::String (outL[static_cast<size_t> (firstDiff)])
                            + " vs " + juce::String (outR[static_cast<size_t> (firstDiff)]));
            expect (identical, "sideband-engaged mono input produced different L/R outputs");
        }

        beginTest ("SB-U11 sideband follows the real F0 tracker (200 -> 300 Hz)");
        {
            // 2 s saw: 1 s at 200 Hz then 1 s at 300 Hz, continuous phase.
            // With engine=1 the output must show k·200 ± ratio·200 sidebands
            // in the first half and k·300 ± ratio·300 in the second (the F0
            // tracker locks well inside the 200 ms budget).
            constexpr double ratio = 0.27;
            std::vector<float> sig (static_cast<size_t> (kTotal));
            double phase = 0.0;
            for (int n = 0; n < kTotal; ++n)
            {
                const double freq = (n < kTotal / 2) ? 200.0 : 300.0;
                phase += freq / kSampleRate;
                phase -= std::floor (phase);
                sig[static_cast<size_t> (n)] = 0.6f * static_cast<float> (2.0 * phase - 1.0);
            }

            params::ParamSnapshot snap;
            snap.engineVersion = 1;
            snap.bandCount = 1;
            snap.bands[0].sidebandDepth = 70.0f;
            snap.bands[0].sidebandFmRatio = static_cast<float> (ratio);
            snap.bands[0].sidebandRoughness = 0.0f;
            snap.bands[0].sidebandLowcut = 100.0f;
            snap.bands[0].sidebandEnable = true;

            EngineRunner r;
            r.engine.setSeed (7);
            std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);
            for (int pos = 0; pos < kTotal; pos += kBlock)
            {
                r.engine.setParameters (snap);
                r.processMono (sig.data() + pos, out.data() + pos, kBlock);
            }

            // Analysis windows: [0.5 s, 0.9 s] and [1.35 s, 1.95 s] — clear of
            // the tracker lock-in (≈50 ms) and the 1 s switch transient.
            const auto sidebandPresent = [&] (int startSample, double f0)
            {
                std::vector<float> win (out.begin() + startSample,
                                        out.begin() + startSample + 19200);
                const double fm = f0 * ratio;
                bool found = false;
                for (int k = 1; k <= 3 && ! found; ++k)
                {
                    for (double offset : { -fm, fm })
                    {
                        const double center = k * f0 + offset;
                        double best = 0.0;
                        for (double f = center - 1.5; f <= center + 1.5; f += 0.1)
                            best = juce::jmax (best, dftMag (win, f));
                        // 0.02: above the rectangular-window sidelobe leakage
                        // of the k·f0 carriers (~0.015 at 54 Hz offset).
                        if (best > 0.02)
                            found = true;
                    }
                }
                return found;
            };

            expect (sidebandPresent (static_cast<int> (kSampleRate * 0.5), 200.0),
                    "no 200 Hz sidebands (F0 tracker did not drive the engine)");
            expect (sidebandPresent (static_cast<int> (kSampleRate * 1.35), 300.0),
                    "no 300 Hz sidebands (F0 tracker did not follow the switch)");
        }

        beginTest ("SB-L03 engine_version switch mid-play is click-free (L03 method)");
        {
            params::ParamSnapshot snap;
            snap.bandCount = 1;
            snap.bands[0].sidebandDepth = 60.0f;
            snap.bands[0].sidebandFmRatio = 0.3f;

            const auto input = makeSine (220.0f, kTotal);
            EngineRunner r;
            r.engine.setSeed (9);
            std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);

            constexpr int blocksPerPhase = 20;   // 20 × 512 ≈ 213 ms per phase
            bool finite = true;
            for (int pos = 0; pos < kTotal; pos += kBlock)
            {
                const int block = pos / kBlock;
                const bool useSideband = (block >= blocksPerPhase && block < 2 * blocksPerPhase)
                                      || (block >= 3 * blocksPerPhase);
                snap.engineVersion = useSideband ? 1 : 0;
                r.engine.setParameters (snap);
                r.processMono (input.data() + pos, out.data() + pos, kBlock);
                for (int i = 0; i < kBlock; ++i)
                    finite = finite && std::isfinite (out[static_cast<size_t> (pos + i)]);
            }
            expect (finite, "engine switch produced non-finite samples");

            // Block-to-block RMS jumps stay small (no click / drop-out).
            double prevRms = 0.0;
            float maxJumpDb = 0.0f;
            for (int pos = 0; pos < kTotal; pos += kBlock)
            {
                double sq = 0.0;
                for (int i = 0; i < kBlock; ++i)
                {
                    const auto v = out[static_cast<size_t> (pos + i)];
                    sq += static_cast<double> (v) * v;
                }
                const double rms = std::sqrt (sq / kBlock);
                if (prevRms > 0.0 && rms > 0.0)
                    maxJumpDb = juce::jmax (maxJumpDb,
                                            static_cast<float> (std::abs (20.0 * std::log10 (rms / prevRms))));
                prevRms = rms;
            }
            expectLessThan (maxJumpDb, 6.0f,
                            "engine switch jumped: max block-RMS step " + juce::String (maxJumpDb) + " dB");

            // No overshoot beyond the input peak × 1.5.
            float peak = 0.0f;
            for (int i = 0; i < kTotal; ++i)
                peak = juce::jmax (peak, std::abs (out[static_cast<size_t> (i)]));
            expectLessThan (peak, 0.5f * 1.5f + 1e-3f,
                            "engine switch overshot: peak " + juce::String (peak));
        }

        beginTest ("SB-L04 dry/wet < 100 % and Band On/off stay time-aligned (T45)");
        {
            // T45: the global dry and the Band On bypass path now use the
            // same fixed sideband baseline Lmax as the band output. An
            // impulse must stay a single impulse, at the same position, for
            // 50 % dry/wet and for Band On = off.
            constexpr double sr = kSampleRate;
            const int lmax = dsp::SidebandEngine::maxLatencySamplesForRate (sr);
            const int impulsePos = static_cast<int> (sr * 0.5);

            const auto runImpulse = [&] (float dryWet, bool bandOn)
            {
                params::ParamSnapshot snap;
                snap.engineVersion = 1;
                snap.bandCount = 1;
                snap.dryWet = dryWet;
                snap.bands[0].sidebandDepth = 0.0f; // wet asleep: pure delayed dry
                snap.bands[0].sidebandEnable = true;
                snap.bands[0].enable = bandOn;

                std::vector<float> input (static_cast<size_t> (kTotal), 0.0f);
                input[static_cast<size_t> (impulsePos)] = 0.5f;
                std::vector<float> out (static_cast<size_t> (kTotal), 0.0f);

                EngineRunner r;
                r.engine.setSeed (77);
                for (int pos = 0; pos < kTotal; pos += kBlock)
                {
                    r.engine.setParameters (snap);
                    r.processMono (input.data() + pos, out.data() + pos, kBlock);
                }

                int peakIndex = 0;
                float peak = 0.0f;
                int significant = 0;
                for (int i = 0; i < kTotal; ++i)
                {
                    const auto a = std::abs (out[static_cast<size_t> (i)]);
                    if (a > peak) { peak = a; peakIndex = i; }
                    if (a > 0.05f)
                        ++significant;
                }
                return std::tuple<int, float, int> { peakIndex - impulsePos, peak, significant };
            };

            const auto [pos50, peak50, count50] = runImpulse (50.0f, true);
            logMessage (juce::String ("  dry/wet 50 %, Band On: peak at +")
                        + juce::String (pos50) + " samples, amp " + juce::String (peak50, 3)
                        + ", significant samples " + juce::String (count50));
            expectWithinAbsoluteError (static_cast<double> (pos50), static_cast<double> (lmax), 1.0, "50 % dry/wet must stay aligned to Lmax");
            expectGreaterThan (peak50, 0.45f, "aligned dry/wet collapsed the impulse");
            expectLessThan (static_cast<float> (count50), 3.0f,
                            "dry/wet produced a double impulse (comb), not one aligned peak");

            const auto [posOff, peakOff, countOff] = runImpulse (100.0f, false);
            logMessage (juce::String ("  Band On off: peak at +")
                        + juce::String (posOff) + " samples, amp " + juce::String (peakOff, 3)
                        + ", significant samples " + juce::String (countOff));
            expectWithinAbsoluteError (static_cast<double> (posOff), static_cast<double> (lmax), 1.0, "Band On off must keep the Lmax delay");
            expectGreaterThan (peakOff, 0.45f, "Band On off collapsed the impulse");
            expectLessThan (static_cast<float> (countOff), 3.0f,
                            "Band On off produced extra peaks");
        }

        // Audit P2-3: changing band_count while playing is a structural
        // routing change (band inputs + crossover state). The output fades
        // out, the routing switches, and fades back in — no click and no
        // stale delay history in the newly enabled bands.
        beginTest ("P2-3 band_count switches are click-free");
        {
            params::ParamSnapshot snap;
            snap.bandCount = 1;
            snap.xover1Freq = 400.0f;
            snap.xover2Freq = 2000.0f;
            snap.bands[0].gain = 6.0f;
            snap.bands[1].gain = -12.0f;
            snap.bands[2].gain = -6.0f;

            EngineRunner engineRunner;
            engineRunner.engine.setParameters (snap);

            constexpr int total = 96 * kBlock;
            const auto in = makeSine (220.0f, total);
            std::vector<float> out (static_cast<size_t> (total), 0.0f);

            bool finite = true;
            float previous = 0.0f;
            bool havePrevious = false;
            float maxJump = 0.0f;

            for (int pos = 0; pos < total; pos += kBlock)
            {
                if (pos == 32 * kBlock)
                {
                    snap.bandCount = 3;
                    engineRunner.engine.setParameters (snap);
                }
                else if (pos == 64 * kBlock)
                {
                    snap.bandCount = 1;
                    engineRunner.engine.setParameters (snap);
                }

                engineRunner.processMono (in.data() + pos, out.data() + pos, kBlock);

                for (int i = 0; i < kBlock; ++i)
                {
                    const auto v = out[static_cast<size_t> (pos + i)];
                    finite = finite && std::isfinite (v);
                    if (havePrevious)
                        maxJump = juce::jmax (maxJump, std::abs (v - previous));
                    previous = v;
                    havePrevious = true;
                }
            }

            logMessage (juce::String ("  band_count 1->3->1 max sample jump = ")
                        + juce::String (maxJump, 4));

            expect (finite, "band-count switching produced non-finite samples");
            expectLessThan (maxJump, 0.1f,
                            "band-count switch produced a click: max jump "
                                + juce::String (maxJump, 4));

            // The routing must be restored after the final switch.
            double sumSq = 0.0;
            const int tail = total - 12 * kBlock;
            for (int i = tail; i < total; ++i)
                sumSq += static_cast<double> (out[static_cast<size_t> (i)]) * out[static_cast<size_t> (i)];
            const auto tailRms = static_cast<float> (std::sqrt (sumSq / (12.0 * kBlock)));
            expectGreaterThan (tailRms, 0.05f, "routing did not come back after the switch");
        }

        // Reversals during the 5 ms fade (small blocks, a change every block)
        // must continue from the current gain, not jump back to unity.
        beginTest ("P2-3 rapid band-count reversals stay continuous");
        {
            constexpr int smallBlock = 64;
            EngineRunner engineRunner (smallBlock);

            params::ParamSnapshot snap;
            snap.bandCount = 1;
            snap.xover1Freq = 400.0f;
            snap.xover2Freq = 2000.0f;
            snap.bands[0].gain = 6.0f;
            snap.bands[1].gain = -12.0f;
            engineRunner.engine.setParameters (snap);

            const auto input = makeSine (220.0f, 60 * smallBlock);
            std::vector<float> out (input.size(), 0.0f);

            float previous = 0.0f;
            bool havePrevious = false;
            float maxJump = 0.0f;

            for (int pos = 0; pos < static_cast<int> (input.size()); pos += smallBlock)
            {
                const auto blockIndex = pos / smallBlock;
                if (blockIndex >= 10) // settle first
                    snap.bandCount = (blockIndex % 2 == 0) ? 3 : 1;
                engineRunner.engine.setParameters (snap);

                engineRunner.processMono (input.data() + pos, out.data() + pos, smallBlock);

                for (int i = 0; i < smallBlock; ++i)
                {
                    const auto v = out[static_cast<size_t> (pos + i)];
                    expect (std::isfinite (v), "rapid reversals produced non-finite samples");
                    if (havePrevious)
                        maxJump = juce::jmax (maxJump, std::abs (v - previous));
                    previous = v;
                    havePrevious = true;
                }
            }

            logMessage (juce::String ("  rapid reversals max sample jump = ")
                        + juce::String (maxJump, 4));
            expectLessThan (maxJump, 0.1f,
                            "rapid band-count reversals clicked: max jump "
                                + juce::String (maxJump, 4));
        }
    }
};

MultibandEngineTest multibandEngineTest;

} // namespace
