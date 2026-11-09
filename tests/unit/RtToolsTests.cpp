/*
    NeoVoiDrive T04 tests — RT toolset.

      1. ParamSnapshot: fillFrom() mirrors the APVTS (denormalised values,
         choice indices, bools) and defaults match SPEC §4;
      2. SpscFifo: ordering, overflow drops without blocking/corruption,
         empty pop, reset, capacity accounting;
      3. SmoothedValue: ramp converges, reset snaps, zero-length jumps,
         isSmoothing lifecycle.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "params/ParamSnapshot.h"
#include "params/ParameterIDs.h"
#include "util/SpscFifo.h"
#include "util/SmoothedValue.h"

#include <array>

namespace
{

//==============================================================================
//  Test 1: ParamSnapshot
//==============================================================================
class ParamSnapshotTest final : public juce::UnitTest
{
public:
    ParamSnapshotTest() : juce::UnitTest ("ParamSnapshot") {}

    void runTest() override
    {
        beginTest ("default snapshot matches SPEC §4 defaults");
        {
            PluginProcessor processor;
            params::ParamSnapshot snapshot;
            snapshot.fillFrom (processor.apvts);

            expect (! snapshot.bypass);
            expect (! snapshot.formantEnabled);
            expectEquals (snapshot.bandCount, 1);   // T37: default 1 band
            expectEquals (snapshot.xoverSlope, 24);
            expectWithinAbsoluteError (snapshot.xover1Freq, 200.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.xover2Freq, 4000.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.formantShift, 0.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.formantQuefrency, 0.5f, 1e-3f);
            expectWithinAbsoluteError (snapshot.dryWet, 100.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.outputTrim, 0.0f, 1e-3f);
            expectEquals (snapshot.engineVersion, 0); // T62: default = Legacy

            for (int band = 0; band < params::numBands; ++band)
            {
                const auto& b = snapshot.bands[band];
                expectWithinAbsoluteError (b.jitterDepth, 0.0f, 1e-3f);
                expectWithinAbsoluteError (b.jitterRate, 100.0f, 1e-3f);
                expectWithinAbsoluteError (b.shimmerDepth, 0.0f, 1e-3f);
                expectWithinAbsoluteError (b.shimmerRate, 100.0f, 1e-3f);
                expectWithinAbsoluteError (b.harmonicDrive, 0.0f, 1e-3f);
                expectWithinAbsoluteError (b.harmonicTone, 3000.0f, 1e-3f);
                expectEquals (b.vibWave, 2); // FullWave (SPEC §4.3 default)
                expectWithinAbsoluteError (b.vibRate, 10.0f, 1e-3f);
                expectWithinAbsoluteError (b.vibDepth, 0.0f, 1e-3f);
                // Sideband defaults (SIDEBAND_SPEC §4.2)
                expect (b.sidebandEnable, "sideband_enable should default on (C4)");
                expectWithinAbsoluteError (b.sidebandDepth, 0.0f, 1e-3f);
                expectWithinAbsoluteError (b.sidebandFmRatio, 0.333f, 1e-3f);
                expectWithinAbsoluteError (b.sidebandRoughness, 20.0f, 1e-3f);
                expectWithinAbsoluteError (b.sidebandLowcut, 2000.0f, 1e-3f);
                expectWithinAbsoluteError (b.fog, 0.0f, 1e-3f);   // T49
            }
        }

        beginTest ("fillFrom mirrors non-default APVTS values (all 68)");
        {
            PluginProcessor processor;

            auto setFloat = [&] (const juce::String& id, float v)
            {
                auto* f = dynamic_cast<juce::AudioParameterFloat*> (processor.apvts.getParameter (id));
                *f = v;
            };
            auto setBool = [&] (const juce::String& id, bool v)
            {
                auto* b = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter (id));
                *b = v;
            };
            auto setChoice = [&] (const juce::String& id, int index)
            {
                auto* c = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id));
                *c = index;
            };

            setFloat  ("xover1_freq",          4321.0f);
            setFloat  ("xover2_freq",          7777.0f);
            setFloat  ("formant_shift",         -6.5f);
            setFloat  ("formant_quefrency",      3.25f);
            setFloat  ("dry_wet",               33.0f);
            setFloat  ("output_trim",           -1.5f);
            setBool   ("bypass",                true);
            setBool   ("formant_enabled",       true);
            setChoice ("band_count",             0);   // "1"
            setChoice ("xover_slope",            3);   // "48" (index 3 of 6)
            setFloat  ("b1_jitter_depth",       12.5f);
            setFloat  ("b1_jitter_rate",       300.0f);
            setFloat  ("b1_shimmer_depth",      77.0f);
            setFloat  ("b1_shimmer_rate",      150.0f);
            setFloat  ("b1_harmonic_drive",     90.0f);
            setFloat  ("b1_harmonic_tone",     6500.0f);
            setChoice ("b1_vib_wave",            2);   // FullWave
            setFloat  ("b1_vib_rate",          150.0f);
            setFloat  ("b1_vib_depth",         150.0f);
            setFloat  ("b2_jitter_depth",        1.0f);
            setFloat  ("b3_shimmer_rate",      400.0f);
            setChoice ("b3_vib_wave",            1);   // Square
            // Sideband (SIDEBAND_SPEC §4.2)
            setBool   ("b1_sideband_enable",    false);
            setFloat  ("b1_sideband_depth",     60.0f);
            setFloat  ("b1_sideband_fm_ratio",   0.4f);
            setFloat  ("b2_sideband_roughness", 80.0f);
            setFloat  ("b3_sideband_lowcut",   1200.0f);
            setFloat  ("b2_sideband_bright",      70.0f);   // T40
            setFloat  ("b1_fog",                  45.0f);   // T49
            setChoice ("engine_version",          1);   // Sideband

            params::ParamSnapshot snapshot;
            snapshot.fillFrom (processor.apvts);

            expect (snapshot.bypass);
            expect (snapshot.formantEnabled);
            expectEquals (snapshot.bandCount, 1);
            expectEquals (snapshot.xoverSlope, 48);
            expectWithinAbsoluteError (snapshot.xover1Freq, 4321.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.xover2Freq, 7777.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.formantShift, -6.5f, 1e-3f);
            expectWithinAbsoluteError (snapshot.formantQuefrency, 3.25f, 1e-3f);
            expectWithinAbsoluteError (snapshot.dryWet, 33.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.outputTrim, -1.5f, 1e-3f);
            expectEquals (snapshot.engineVersion, 1);

            const auto& b1 = snapshot.bands[0];
            expectWithinAbsoluteError (b1.jitterDepth, 12.5f, 1e-3f);
            expectWithinAbsoluteError (b1.jitterRate, 300.0f, 1e-3f);
            expectWithinAbsoluteError (b1.shimmerDepth, 77.0f, 1e-3f);
            expectWithinAbsoluteError (b1.shimmerRate, 150.0f, 1e-3f);
            expectWithinAbsoluteError (b1.harmonicDrive, 90.0f, 1e-3f);
            expectWithinAbsoluteError (b1.harmonicTone, 6500.0f, 1e-3f);
            expectEquals (b1.vibWave, 2);
            expectWithinAbsoluteError (b1.vibRate, 150.0f, 1e-3f);
            expectWithinAbsoluteError (b1.vibDepth, 150.0f, 1e-3f);
            expect (! b1.sidebandEnable);
            expectWithinAbsoluteError (b1.sidebandDepth, 60.0f, 1e-3f);
            expectWithinAbsoluteError (b1.sidebandFmRatio, 0.4f, 1e-3f);

            expectWithinAbsoluteError (snapshot.bands[1].jitterDepth, 1.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.bands[1].sidebandRoughness, 80.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.bands[2].shimmerRate, 400.0f, 1e-3f);
            expectEquals (snapshot.bands[2].vibWave, 1);
            expectWithinAbsoluteError (snapshot.bands[2].sidebandLowcut, 1200.0f, 1e-3f);
            expectWithinAbsoluteError (snapshot.bands[1].sidebandBright, 70.0f, 1e-3f);   // T40
            expectWithinAbsoluteError (snapshot.bands[0].fog, 45.0f, 1e-3f);             // T49
        }

        beginTest ("snapshot is trivially copyable (POD)");
        {
            expect (std::is_trivially_copyable_v<params::ParamSnapshot>);
        }

        beginTest ("compile-time band IDs agree with makeBandParameterID");
        {
            // The IDs::b{n}_* literals used by the RT path must stay in sync
            // with the layout factory's generator.
            constexpr const char* fullIDs[params::numBands][params::numBandParams] =
            {
                { params::IDs::b1_jitter_depth,   params::IDs::b1_jitter_rate,    params::IDs::b1_shimmer_depth,  params::IDs::b1_shimmer_rate,
                  params::IDs::b1_harmonic_drive, params::IDs::b1_harmonic_tone,  params::IDs::b1_vib_wave,       params::IDs::b1_vib_rate,   params::IDs::b1_vib_depth,
                  params::IDs::b1_gain, params::IDs::b1_enable },
                { params::IDs::b2_jitter_depth,   params::IDs::b2_jitter_rate,    params::IDs::b2_shimmer_depth,  params::IDs::b2_shimmer_rate,
                  params::IDs::b2_harmonic_drive, params::IDs::b2_harmonic_tone,  params::IDs::b2_vib_wave,       params::IDs::b2_vib_rate,   params::IDs::b2_vib_depth,
                  params::IDs::b2_gain, params::IDs::b2_enable },
                { params::IDs::b3_jitter_depth,   params::IDs::b3_jitter_rate,    params::IDs::b3_shimmer_depth,  params::IDs::b3_shimmer_rate,
                  params::IDs::b3_harmonic_drive, params::IDs::b3_harmonic_tone,  params::IDs::b3_vib_wave,       params::IDs::b3_vib_rate,   params::IDs::b3_vib_depth,
                  params::IDs::b3_gain, params::IDs::b3_enable },
            };

            const char* bases[params::numBandParams] =
            {
                params::IDs::jitterDepth,   params::IDs::jitterRate,    params::IDs::shimmerDepth,  params::IDs::shimmerRate,
                params::IDs::harmonicDrive, params::IDs::harmonicTone,  params::IDs::vibWave,       params::IDs::vibRate,   params::IDs::vibDepth,
                params::IDs::gain, params::IDs::enable,
            };

            for (int band = 1; band <= params::numBands; ++band)
                for (int p = 0; p < params::numBandParams; ++p)
                    expectEquals (params::makeBandParameterID (band, bases[p]),
                                  juce::String (fullIDs[band - 1][p]),
                                  juce::String ("band ") + juce::String (band) + " param " + bases[p]);
        }

        beginTest ("compile-time sideband band IDs agree with makeBandParameterID");
        {
            constexpr const char* fullIDs[params::numBands][params::numSidebandBandParams] =
            {
                { params::IDs::b1_sideband_enable,    params::IDs::b1_sideband_depth,
                  params::IDs::b1_sideband_fm_ratio,  params::IDs::b1_sideband_roughness,
                  params::IDs::b1_sideband_lowcut,    params::IDs::b1_sideband_bright },
                { params::IDs::b2_sideband_enable,    params::IDs::b2_sideband_depth,
                  params::IDs::b2_sideband_fm_ratio,  params::IDs::b2_sideband_roughness,
                  params::IDs::b2_sideband_lowcut,    params::IDs::b2_sideband_bright },
                { params::IDs::b3_sideband_enable,    params::IDs::b3_sideband_depth,
                  params::IDs::b3_sideband_fm_ratio,  params::IDs::b3_sideband_roughness,
                  params::IDs::b3_sideband_lowcut,    params::IDs::b3_sideband_bright },
            };

            const char* bases[params::numSidebandBandParams] =
            {
                params::IDs::sidebandEnable,   params::IDs::sidebandDepth,
                params::IDs::sidebandFmRatio,  params::IDs::sidebandRoughness,
                params::IDs::sidebandLowcut,   params::IDs::sidebandBright,
            };

            for (int band = 1; band <= params::numBands; ++band)
                for (int p = 0; p < params::numSidebandBandParams; ++p)
                    expectEquals (params::makeBandParameterID (band, bases[p]),
                                  juce::String (fullIDs[band - 1][p]),
                                  juce::String ("band ") + juce::String (band) + " sideband param " + bases[p]);
        }

        beginTest ("compile-time band-fog IDs agree with makeBandParameterID (T49)");
        {
            constexpr const char* fullIDs[params::numBands][params::numBandFogParams] =
            {
                { params::IDs::b1_fog },
                { params::IDs::b2_fog },
                { params::IDs::b3_fog },
            };

            for (int band = 1; band <= params::numBands; ++band)
                for (int p = 0; p < params::numBandFogParams; ++p)
                    expectEquals (params::makeBandParameterID (band, params::IDs::fog),
                                  juce::String (fullIDs[band - 1][p]),
                                  juce::String ("band ") + juce::String (band) + " fog param");
        }
    }
};

//==============================================================================
//  Test 2: SpscFifo
//==============================================================================
class SpscFifoTest final : public juce::UnitTest
{
public:
    SpscFifoTest() : juce::UnitTest ("SpscFifo") {}

    void runTest() override
    {
        beginTest ("push/pop preserves order");
        {
            util::SpscFifo<int, 8> fifo;

            for (int i = 0; i < 8; ++i)
                expect (fifo.push (i * 10));

            expectEquals (fifo.available(), 8);
            expectEquals (fifo.freeSpace(), 0);

            for (int i = 0; i < 8; ++i)
            {
                int value = -1;
                expect (fifo.pop (value));
                expectEquals (value, i * 10);
            }

            expectEquals (fifo.available(), 0);
        }

        beginTest ("overflow drops newest items without blocking or corrupting");
        {
            util::SpscFifo<int, 8> fifo;
            int dropped = 0;

            for (int i = 0; i < 20; ++i)
                if (! fifo.push (i))
                    ++dropped;

            // 12 overflow attempts (20 - 8), none of them blocking
            expectEquals (dropped, 12);
            expectEquals (fifo.available(), 8);

            // surviving items are the first 8 pushed, in order (write side
            // refuses instead of overwriting, so nothing is corrupted)
            for (int i = 0; i < 8; ++i)
            {
                int value = -1;
                expect (fifo.pop (value));
                expectEquals (value, i);
            }

            int value = -1;
            expect (! fifo.pop (value)); // empty now
        }

        beginTest ("pop on empty returns false");
        {
            util::SpscFifo<float, 4> fifo;
            float value = 0.0f;
            expect (! fifo.pop (value));
        }

        beginTest ("reset empties the buffer");
        {
            util::SpscFifo<int, 4> fifo;
            for (int i = 0; i < 4; ++i)
                fifo.push (i);

            fifo.reset();
            expectEquals (fifo.available(), 0);
            expectEquals (fifo.freeSpace(), 4);

            int value = -1;
            expect (! fifo.pop (value));
        }

        beginTest ("capacity accounting after partial fill");
        {
            util::SpscFifo<int, 16> fifo;
            for (int i = 0; i < 5; ++i)
                fifo.push (i);

            expectEquals (fifo.available(), 5);
            expectEquals (fifo.freeSpace(), 11);

            int value = -1;
            fifo.pop (value);
            expectEquals (value, 0);
            expectEquals (fifo.available(), 4);
            expectEquals (fifo.freeSpace(), 12);
        }

        beginTest ("large capacity template (SPEC §5.2: 16384)");
        {
            util::SpscFifo<float, 16384> fifo;
            for (int i = 0; i < 16384; ++i)
                expect (fifo.push (static_cast<float> (i)));

            expectEquals (fifo.available(), 16384);
            expect (! fifo.push (0.0f)); // full

            float value = 0.0f;
            expect (fifo.pop (value));
            expectEquals (value, 0.0f);
        }

        beginTest ("wrap-around keeps order across buffer boundaries");
        {
            util::SpscFifo<int, 4> fifo;

            // fill, drain one, refill -> forces the write index to wrap and
            // exercises the storage slot at index == Capacity (the backing
            // buffer must be bufferSize == Capacity+1 slots)
            for (int i = 0; i < 4; ++i)
                expect (fifo.push (i));

            int value = -1;
            expect (fifo.pop (value));
            expectEquals (value, 0);

            expect (fifo.push (42)); // must succeed and not overrun storage

            for (int i = 1; i < 4; ++i)
            {
                expect (fifo.pop (value));
                expectEquals (value, i);
            }
            expect (fifo.pop (value));
            expectEquals (value, 42);
        }
    }
};

//==============================================================================
//  Test 3: SmoothedValue wrapper
//==============================================================================
class SmoothedValueTest final : public juce::UnitTest
{
public:
    SmoothedValueTest() : juce::UnitTest ("SmoothedValue") {}

    void runTest() override
    {
        beginTest ("ramp converges linearly to the target");
        {
            constexpr double sampleRate = 48000.0;
            constexpr double rampMs = 10.0;

            util::SmoothedValue sv;
            sv.prepare (sampleRate, rampMs);
            sv.reset (0.0f);
            sv.setTarget (1.0f);

            expect (sv.isSmoothing());

            const auto rampSamples = static_cast<int> (rampMs * 0.001 * sampleRate);

            float previous = sv.getNext();
            float firstStep = previous;
            expectGreaterThan (firstStep, 0.0f); // starts moving immediately
            expectLessThan (firstStep, 1.0f);

            // Linear increments, except the final sample which snaps exactly to
            // the target (JUCE semantics: countdown reaches 0 -> current=target).
            for (int i = 1; i < rampSamples - 1; ++i)
            {
                const auto current = sv.getNext();
                expectWithinAbsoluteError (current - previous, firstStep, 1e-5f); // linear
                previous = current;
            }

            // final sample lands exactly on the target; smoothing ends
            const auto finalValue = sv.getNext();
            expectWithinAbsoluteError (finalValue, 1.0f, 1e-3f);
            expect (! sv.isSmoothing());
        }

        beginTest ("reset snaps immediately");
        {
            util::SmoothedValue sv;
            sv.prepare (48000.0, 50.0);
            sv.reset (0.5f);
            expectEquals (sv.getNext(), 0.5f);
            expect (! sv.isSmoothing());
        }

        beginTest ("zero ramp length jumps directly");
        {
            util::SmoothedValue sv;
            sv.prepare (48000.0, 0.0);
            sv.reset (0.0f);
            sv.setTarget (3.0f);
            expect (! sv.isSmoothing());
            expectEquals (sv.getNext(), 3.0f);
        }

        beginTest ("setting the same target does not restart the ramp");
        {
            util::SmoothedValue sv;
            sv.prepare (48000.0, 10.0);
            sv.reset (0.0f);
            sv.setTarget (1.0f);
            sv.setTarget (1.0f); // no-op
            expect (sv.isSmoothing());
        }

        beginTest ("snapTo jumps immediately, discarding any in-flight ramp");
        {
            util::SmoothedValue sv;
            sv.prepare (48000.0, 10.0);
            sv.reset (0.0f);
            sv.setTarget (1.0f);          // start a ramp
            expect (sv.isSmoothing());

            sv.snapTo (0.25f);            // mid-ramp snap
            expect (! sv.isSmoothing());
            expectEquals (sv.getNext(), 0.25f);
            expectEquals (sv.getNext(), 0.25f); // stays put
        }

        beginTest ("ramp duration scales with sample rate");
        {
            util::SmoothedValue sv;
            sv.prepare (96000.0, 10.0); // double the rate -> double the samples
            sv.reset (0.0f);
            sv.setTarget (1.0f);

            const auto rampSamples = static_cast<int> (10.0 * 0.001 * 96000.0);
            float value = 0.0f;
            for (int i = 0; i < rampSamples; ++i)
                value = sv.getNext();

            expectWithinAbsoluteError (value, 1.0f, 1e-3f);
        }
    }
};

ParamSnapshotTest paramSnapshotTest;
SpscFifoTest spscFifoTest;
SmoothedValueTest smoothedValueTest;

} // namespace
