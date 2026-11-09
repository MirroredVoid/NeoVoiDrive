/*
    NeoVoiDrive T03 tests — processor skeleton & dynamic latency framework.

      1. LatencyManager: two-state latency report (0 / 2048) flips only at
         block boundaries; 5 ms crossfade ramp converges and is not restarted
         by duplicate calls; ramp length scales with sample rate;
      2. BypassRamp: 5 ms equal-gain ramp is linear with no jumps (mechanism
         level; end-to-end audition arrives with the offline renderer, T21);
      3. ProcessorSkeleton: pass-through stays bit-transparent under bypass,
         latency follows formant_enabled, repeated blocks are stable.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

#include "PluginProcessor.h"
#include "dsp/LatencyManager.h"
#include "dsp/SidebandEngine.h"   // T43: fog baseline reported at depth 0 too
#include "params/ParamSnapshot.h"
#include "params/ParameterIDs.h"
#include "util/SmoothedValue.h"

namespace
{

//==============================================================================
//  Test 1: LatencyManager
//==============================================================================
class LatencyManagerTest final : public juce::UnitTest
{
public:
    LatencyManagerTest() : juce::UnitTest ("LatencyManager") {}

    void runTest() override
    {
        beginTest ("default state: off, zero latency, no crossfade");
        {
            PluginProcessor processor;
            dsp::LatencyManager lm;
            lm.prepare (48000.0);

            expect (! lm.isFormantEnabled());
            expectEquals (lm.getCurrentLatencySamples(), 0);
            expect (! lm.isCrossfading());
            expectEquals (lm.getNextGain(), 0.0f);
        }

        beginTest ("enable reports 2048 samples, holds gain 0, then ramps 0->1");
        {
            PluginProcessor processor;
            dsp::LatencyManager lm;
            lm.prepare (48000.0);

            lm.prepareBlock (true, processor);

            expect (lm.isFormantEnabled());
            expectEquals (processor.getLatencySamples(), dsp::LatencyManager::formantLatencySamples);
            expect (lm.isCrossfading());

            // S2 (T13 review): the gain must stay at 0 for the full engine
            // latency (first valid frame), then ramp linearly over 5 ms.
            constexpr int holdSamples = dsp::LatencyManager::formantLatencySamples;
            for (int i = 0; i < holdSamples; ++i)
                expectEquals (lm.getNextGain(), 0.0f);
            expect (lm.isCrossfading());

            // 5 ms @ 48 kHz = 240 samples; gain ramps linearly to 1
            constexpr int rampSamples = 240;
            float previous = lm.getNextGain();
            const float firstStep = previous;
            expectGreaterThan (firstStep, 0.0f);
            expectLessThan (firstStep, 1.0f);

            for (int i = 1; i < rampSamples - 1; ++i)
            {
                const auto current = lm.getNextGain();
                expectWithinAbsoluteError (current - previous, firstStep, 1e-5f); // linear, constant step
                previous = current;
            }

            const auto finalGain = lm.getNextGain();
            expectWithinAbsoluteError (finalGain, 1.0f, 1e-3f);
            expect (! lm.isCrossfading());
        }

        beginTest ("disable reports 0 samples and ramps gain 1->0");
        {
            PluginProcessor processor;
            dsp::LatencyManager lm;
            lm.prepare (48000.0);

            lm.prepareBlock (true, processor);
            for (int i = 0; i < dsp::LatencyManager::formantLatencySamples + 240; ++i)
                lm.getNextGain(); // finish the hold + fade-in

            lm.prepareBlock (false, processor);

            expect (! lm.isFormantEnabled());
            expectEquals (processor.getLatencySamples(), 0);
            expect (lm.isCrossfading());

            // gain ramps 1 -> 0 with a constant (negative) step
            float previous = lm.getNextGain();
            const float firstStep = previous - 1.0f;
            expectLessThan (firstStep, 0.0f);

            for (int i = 1; i < 239; ++i)
            {
                const auto current = lm.getNextGain();
                expectWithinAbsoluteError (current - previous, firstStep, 1e-5f);
                previous = current;
            }

            expectWithinAbsoluteError (lm.getNextGain(), 0.0f, 1e-3f);
            expect (! lm.isCrossfading());
        }

        beginTest ("disable during the hold goes straight off (never engaged)");
        {
            PluginProcessor processor;
            dsp::LatencyManager lm;
            lm.prepare (48000.0);

            lm.prepareBlock (true, processor);
            lm.getNextGain(); // still holding (gain 0)
            expectEquals (lm.getNextGain(), 0.0f);

            lm.prepareBlock (false, processor); // disable before any data
            expect (! lm.isCrossfading());
            expectEquals (lm.getNextGain(), 0.0f); // no ramp, no jump
        }

        beginTest ("duplicate state does not restart the crossfade");
        {
            PluginProcessor processor;
            dsp::LatencyManager lm;
            lm.prepare (48000.0);

            lm.prepareBlock (true, processor);
            lm.prepareBlock (true, processor); // no-op
            expect (lm.isCrossfading()); // holding

            // Let it fully engage, then disable; duplicates stay no-ops.
            for (int i = 0; i < dsp::LatencyManager::formantLatencySamples + 240; ++i)
                lm.getNextGain();
            expect (! lm.isCrossfading()); // engaged

            lm.prepareBlock (false, processor);
            lm.prepareBlock (false, processor); // no-op
            expect (lm.isCrossfading()); // ramping down
        }

        beginTest ("crossfade length scales with sample rate");
        {
            PluginProcessor processor;
            dsp::LatencyManager lm;
            lm.prepare (96000.0); // double rate -> 480 samples

            lm.prepareBlock (true, processor);

            // Hold is fixed at the engine latency (2048 samples), the ramp
            // scales: 5 ms @ 96 kHz = 480 samples.
            for (int i = 0; i < dsp::LatencyManager::formantLatencySamples + 479; ++i)
                lm.getNextGain();

            const auto finalGain = lm.getNextGain();
            expectWithinAbsoluteError (finalGain, 1.0f, 1e-3f);
            expect (! lm.isCrossfading());
        }
    }
};

//==============================================================================
//  Test 2: bypass ramp mechanism (5 ms equal-gain, DECISIONS §3.3)
//==============================================================================
class BypassRampTest final : public juce::UnitTest
{
public:
    BypassRampTest() : juce::UnitTest ("BypassRamp") {}

    void runTest() override
    {
        beginTest ("5 ms ramp is linear with no jump");
        {
            util::SmoothedValue ramp;
            ramp.prepare (48000.0, 5.0);
            ramp.reset (1.0f); // not bypassed

            ramp.setTarget (0.0f); // bypass engaged

            expect (ramp.isSmoothing());

            constexpr int rampSamples = 240;
            float previous = ramp.getNext();
            const float firstStep = previous - 1.0f; // downward step (negative)
            expectLessThan (firstStep, 0.0f);
            expectGreaterThan (firstStep, -0.1f); // no instant jump at the first sample

            for (int i = 1; i < rampSamples - 1; ++i)
            {
                const auto current = ramp.getNext();
                expectWithinAbsoluteError (current - previous, firstStep, 1e-5f);
                previous = current;
            }

            expectWithinAbsoluteError (ramp.getNext(), 0.0f, 1e-3f);
            expect (! ramp.isSmoothing());
        }

        beginTest ("ramp back to unity when bypass disengages");
        {
            util::SmoothedValue ramp;
            ramp.prepare (48000.0, 5.0);
            ramp.reset (0.0f); // bypassed

            ramp.setTarget (1.0f);
            for (int i = 0; i < 240; ++i)
                ramp.getNext();

            expectWithinAbsoluteError (ramp.getNext(), 1.0f, 1e-3f);
        }
    }
};

//==============================================================================
//  Test 3: processor skeleton integration
//==============================================================================
class ProcessorSkeletonTest final : public juce::UnitTest
{
public:
    ProcessorSkeletonTest() : juce::UnitTest ("ProcessorSkeleton") {}

    void runTest() override
    {
        beginTest ("default chain output is bounded and near-unity");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            auto fillBuffer = [] (juce::AudioBuffer<float>& b)
            {
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        b.setSample (ch, i, static_cast<float> (std::sin (0.01 * i + ch)) * 0.5f);
            };

            juce::AudioBuffer<float> input (2, 512);
            fillBuffer (input);

            auto reference = input; // copy

            juce::MidiBuffer midi;
            processor.processBlock (input, midi);

            // T13: with the real chain in place, the default parameters still
            // pass through transparent units, but the crossover sum adds
            // phase and Linkwitz-Riley ripple — assert bounded, near-unity
            // gain and no NaN instead of bit-transparency (the bypass test
            // below covers exact passthrough).
            double inSq = 0.0, outSq = 0.0;
            bool finite = true;
            for (int ch = 0; ch < input.getNumChannels(); ++ch)
                for (int i = 0; i < input.getNumSamples(); ++i)
                {
                    const auto v = input.getSample (ch, i);
                    inSq += static_cast<double> (reference.getSample (ch, i))
                          * reference.getSample (ch, i);
                    outSq += static_cast<double> (v) * v;
                    finite = finite && std::isfinite (v);
                }

            expect (finite, "default chain produced non-finite samples");
            const auto db = 10.0 * std::log10 (outSq / juce::jmax (inSq, 1e-30));
            expect (std::abs (db) < 2.0,
                    juce::String ("default chain gain ") + juce::String (db) + " dB");
        }

        beginTest ("bypass keeps pass-through transparent (after the 5 ms ramp)");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            // Pin the Legacy engine: this test checks sample-identical bypass
            // in the zero-latency path. The Sideband engine's bypass is now
            // latency-aligned by design (T45) and is covered by the latency
            // impulse matrix.
            if (auto* engine = dynamic_cast<juce::AudioParameterChoice*> (
                    processor.apvts.getParameter ("engine_version")))
                *engine = 0;

            auto* bypass = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter ("bypass"));
            *bypass = true;

            juce::AudioBuffer<float> buffer (2, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, static_cast<float> (i) * 0.001f);

            auto reference = buffer;

            juce::MidiBuffer midi;
            // First block rides the 5 ms equal-gain ramp (T13: the processed
            // chain is no longer identical to the input, so the ramp mixes);
            // the second block is fully dry and must equal the input exactly.
            processor.processBlock (buffer, midi);
            buffer = reference; // re-fill: the first pass consumed the buffer
            processor.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expectEquals (buffer.getSample (ch, i), reference.getSample (ch, i));
        }

        beginTest ("latency follows engine and formant_enabled across blocks");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;

            // T62: new instances default to Legacy -> 0 samples.
            processor.processBlock (buffer, midi);
            expectEquals (processor.getLatencySamples(), 0);

            // Explicit Sideband reports the fixed wet-path baseline Lmax
            // (T45); the extra latency is published from processBlock.
            auto* engine = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter ("engine_version"));
            expect (engine != nullptr, "engine_version missing");
            if (engine == nullptr)
                return;
            const auto sbBase = dsp::SidebandEngine::maxLatencySamplesForRate (48000.0);
            *engine = 1;
            processor.processBlock (buffer, midi);
            expectEquals (processor.getLatencySamples(), sbBase);

            auto* formant = dynamic_cast<juce::AudioParameterBool*> (
                processor.apvts.getParameter ("formant_enabled"));
            *formant = true;
            processor.processBlock (buffer, midi);
            expectEquals (processor.getLatencySamples(),
                          dsp::LatencyManager::formantLatencySamples + sbBase);

            *formant = false;
            processor.processBlock (buffer, midi);
            expectEquals (processor.getLatencySamples(), sbBase);

            // Back to Legacy -> 0 again.
            *engine = 0;
            processor.processBlock (buffer, midi);
            expectEquals (processor.getLatencySamples(), 0);
        }

        beginTest ("formant enable keeps output continuous (L03b/S2)");
        {
            // dry_wet=100%: enabling formant must NOT create a ~43 ms silence
            // gap (pre-fix: the fg ramp faded in while the engine's first
            // 2048 output samples were still zero). The hold phase keeps the
            // wet path at sum, so energy must stay continuous.
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            auto* formant = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter ("formant_enabled"));
            auto* dryWet  = dynamic_cast<juce::AudioParameterFloat*> (processor.apvts.getParameter ("dry_wet"));
            *dryWet = 100.0f;

            constexpr int totalBlocks = 128; // ~2.7 s @ 512
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (ch, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                              * 440.0f * (i + 1) / 48000.0f));

            // Steady state with formant off.
            for (int b = 0; b < 20; ++b)
                processor.processBlock (buffer, midi);

            // Capture the steady-state RMS (chain output).
            double ssSq = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    ssSq += static_cast<double> (buffer.getSample (ch, i)) * buffer.getSample (ch, i);
            const auto ssRms = std::sqrt (ssSq / (2.0 * 512.0));

            // Enable and run past the hold + ramp + first frames.
            *formant = true;
            for (int b = 0; b < totalBlocks; ++b)
            {
                processor.processBlock (buffer, midi);

                double sq = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        sq += static_cast<double> (buffer.getSample (ch, i)) * buffer.getSample (ch, i);
                const auto rms = std::sqrt (sq / (2.0 * 512.0));

                // No 10 ms window may drop more than 20 dB below steady state
                // (a 43 ms silence gap would floor this to ~0).
                expectGreaterThan (static_cast<float> (rms), static_cast<float> (ssRms * 0.1f),
                                   juce::String ("energy gap at block ") + juce::String (b));
            }
        }

        beginTest ("processBlock tolerates blocks larger than declared (M3)");
        {
            // Some hosts (offline renderers) deliver more samples than
            // prepareToPlay declared: the chunked processing must not
            // overrun the preallocated gain arrays.
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> buffer (2, 4096);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 4096; ++i)
                    buffer.setSample (ch, i, 0.3f * std::sin (0.01f * static_cast<float> (i + ch)));

            auto* formant = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter ("formant_enabled"));
            *formant = true; // engage the crossfade arrays too

            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);

            bool finite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 4096; ++i)
                    finite = finite && std::isfinite (buffer.getSample (ch, i));
            expect (finite, "oversized block produced non-finite samples");
        }

        beginTest ("repeated processing is stable");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 128);

            juce::AudioBuffer<float> buffer (2, 128);
            juce::MidiBuffer midi;

            for (int block = 0; block < 100; ++block)
                processor.processBlock (buffer, midi);

            expect (true); // reached here without crashing / corrupting
        }

        beginTest ("engine defaults Legacy; sideband parameters keep their defaults (T62)");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            // T62: the Sideband UI is hidden and the engine default returns to
            // Legacy. The parameter itself stays registered so presets and
            // automation can still select Sideband.
            auto* engine = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter ("engine_version"));
            expect (engine != nullptr, "engine_version missing");
            if (engine != nullptr)
                expectEquals (engine->getIndex(), 0);

            // T26 acceptance #4: sideband_depth defaults to 0 — the landing
            // point where the wet path contributes nothing (the DSP-level
            // zero-output guarantee itself is verified in T28).
            for (int band = 1; band <= params::numBands; ++band)
            {
                auto* depth = dynamic_cast<juce::AudioParameterFloat*> (
                    processor.apvts.getParameter (params::makeBandParameterID (band, params::IDs::sidebandDepth)));
                expect (depth != nullptr, params::makeBandParameterID (band, params::IDs::sidebandDepth) + " missing");
                if (depth != nullptr)
                    expectWithinAbsoluteError (static_cast<float> (*depth), 0.0f, 1e-3f);
            }

            // The snapshot now carries the 16 new fields; a default fill must
            // land on Sideband + silent wet path and stay trivially copyable.
            params::ParamSnapshot snapshot;
            snapshot.fillFrom (processor.apvts);
            expectEquals (snapshot.engineVersion, 0);
            for (const auto& b : snapshot.bands)
            {
                expect (b.sidebandEnable);
                expectWithinAbsoluteError (b.sidebandDepth, 0.0f, 1e-3f);
            }
            expect (std::is_trivially_copyable_v<params::ParamSnapshot>);
        }
    }
};

LatencyManagerTest latencyManagerTest;
BypassRampTest bypassRampTest;
ProcessorSkeletonTest processorSkeletonTest;

} // namespace
