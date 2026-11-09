/*
    NeoVoiDrive T02 tests — parameter layout completeness and state persistence.

    Asserts every value from SPEC §4 mechanically:
      1. layout completeness: 65 parameters, IDs word-for-word identical to SPEC;
      2. ranges / defaults / steps / log mappings per SPEC table;
      3. state persistence: XML round-trip preserves all parameters; missing
         parameters fall back to defaults; version attribute is tolerated.

    These tests instantiate the real PluginProcessor so the production
    getStateInformation/setStateInformation code paths are exercised.

    JUCE 9 note: AudioParameterFloat/Choice/Bool hide setValue(); values are
    assigned through their public operator=, and defaults are read through the
    AudioProcessorParameter base class (normalised).
*/

#include <juce_audio_processors/juce_audio_processors.h>  // also pulls in juce_core

#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

#include <cmath>
#include <set>

namespace
{

//==============================================================================
//  Expected values, mechanically transcribed from SPEC §4.
//==============================================================================
struct ExpectedFloatParam
{
    const char* id;
    float min, max, interval, defaultValue;
    bool isLog;
};

const ExpectedFloatParam globalFloatParams[] =
{
    { "xover1_freq",        40.0f, 18000.0f, 0.0f,   200.0f,   true  },
    { "xover2_freq",        40.0f, 18000.0f, 0.0f,   4000.0f,  true  },
    { "formant_shift",     -12.0f,    12.0f, 0.1f,     0.0f,   false },
    { "formant_quefrency",   0.5f,     5.0f, 0.0f,     0.5f,   true  },
    { "dry_wet",             0.0f,   100.0f, 0.0f,   100.0f,   false },
    { "output_trim",       -12.0f,    12.0f, 0.1f,     0.0f,   false },
    { "growl_amount",        0.0f,   100.0f, 0.0f,     0.0f,   false },   // T66
    { "growl_rate",          0.15f,    0.30f, 0.001f,   0.20f,  false },   // T66
    { "growl_tone",       -100.0f,   100.0f, 1.0f,     0.0f,   false },   // T66
};

const ExpectedFloatParam bandFloatParams[] =
{
    { "jitter_depth",    0.0f, 1200.0f,   0.0f,    0.0f, false },
    { "jitter_rate",    10.0f, 1000.0f,   0.0f,  100.0f, true  },
    { "shimmer_depth",   0.0f,  100.0f,   0.0f,    0.0f, false },
    { "shimmer_rate",   10.0f, 1000.0f,   0.0f,  100.0f, true  },
    { "harmonic_drive",  0.0f,  100.0f,   0.0f,    0.0f, false },
    { "harmonic_tone", 1000.0f,10000.0f,  0.0f, 3000.0f, true  },
    { "vib_rate",       10.0f,  300.0f,   0.0f,   10.0f, true  },
    { "vib_depth",       0.0f,  200.0f,   0.0f,    0.0f, false },
    { "gain",          -48.0f,   12.0f,   0.0f,    0.0f, false },   // T37: band gain
    { "fog",             0.0f,  100.0f,   0.0f,    0.0f, false },   // T49: whole-band fog
};

// Sideband per-band params, transcribed from SIDEBAND_SPEC §4.2.
const ExpectedFloatParam bandSidebandFloatParams[] =
{
    { "sideband_depth",     0.0f,  100.0f,  0.0f,    0.0f,  false },
    { "sideband_fm_ratio",  0.0f,    0.5f,  0.001f,  0.333f, false },
    { "sideband_roughness", 0.0f,  100.0f,  0.0f,   20.0f,  false },
    { "sideband_lowcut",  100.0f, 8000.0f,  0.0f, 2000.0f,  true  },
    { "sideband_bright",    0.0f,  100.0f,  0.0f,    0.0f,  false },   // T40
};

struct ExpectedChoiceParam
{
    const char* id;
    juce::StringArray choices;
    int defaultIndex;
};

const ExpectedChoiceParam globalChoiceParams[] =
{
    { "band_count",  { "1", "2", "3" },             0 },   // T37: default 1 band
    { "xover_slope", { "12", "24", "36", "48", "60", "72" }, 1 },
    { "engine_version", { "Legacy (Harmonic Exciter)", "Sideband Roughness" }, 0 },  // T62: default = Legacy
};

const ExpectedChoiceParam bandChoiceParams[] =
{
    { "vib_wave", { "Sine", "Square", "FullWave", "Noise" }, 2 },
};

const char* globalBoolParams[] = { "bypass", "formant_enabled" };
const char* bandSidebandBoolParams[] = { "sideband_enable" };
const char* bandBoolParams[] = { "enable" };   // T38 band master switch

//==============================================================================
//  Helpers
//==============================================================================
std::set<juce::String> allParameterIDs()
{
    std::set<juce::String> ids;

    for (auto& g : globalFloatParams)          ids.insert (g.id);
    for (auto& c : globalChoiceParams)         ids.insert (c.id);
    for (auto& b : globalBoolParams)           ids.insert (b);

    for (int band = 1; band <= params::numBands; ++band)
    {
        for (auto& f : bandFloatParams)        ids.insert (params::makeBandParameterID (band, f.id));
        for (auto& c : bandChoiceParams)       ids.insert (params::makeBandParameterID (band, c.id));
        for (auto& f : bandSidebandFloatParams) ids.insert (params::makeBandParameterID (band, f.id));
        for (auto& b : bandSidebandBoolParams)  ids.insert (params::makeBandParameterID (band, b));
        for (auto& b : bandBoolParams)          ids.insert (params::makeBandParameterID (band, b));   // T38
    }

    ids.insert ("engine_version");

    return ids;
}

juce::AudioProcessorParameter* findParam (PluginProcessor& p, const juce::String& id)
{
    return p.apvts.getParameter (id);
}

// JUCE 9: getParameterID() lives on AudioProcessorParameterWithID (the base
// AudioProcessorParameter does not expose it).
juce::String paramID (juce::AudioProcessorParameter* p)
{
    auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p);
    jassert (withID != nullptr);
    return withID != nullptr ? withID->getParameterID() : juce::String();
}

// JUCE 9: AudioParameterFloat/Choice hide getDefaultValue() behind a private
// override; call the public base-class virtual instead (returns normalised).
float paramDefaultValue (juce::AudioProcessorParameter* p)
{
    return p->getDefaultValue();
}

void setFloat (PluginProcessor& p, const juce::String& id, float value)
{
    auto* f = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (id));
    jassert (f != nullptr);
    *f = value;
}

void setBool (PluginProcessor& p, const juce::String& id, bool value)
{
    auto* b = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter (id));
    jassert (b != nullptr);
    *b = value;
}

void setChoice (PluginProcessor& p, const juce::String& id, int index)
{
    auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id));
    jassert (c != nullptr);
    *c = index;
}

//==============================================================================
//  Test 1: layout completeness + ranges + defaults + log mappings
//==============================================================================
class ParameterLayoutTest final : public juce::UnitTest
{
public:
    ParameterLayoutTest() : juce::UnitTest ("ParameterLayout") {}

    void runTest() override
    {
        PluginProcessor processor;

        beginTest ("layout contains exactly the 68 parameter IDs (SPEC §4)");
        {
            std::set<juce::String> actual;
            for (auto* param : processor.getParameters())
                actual.insert (paramID (param));

            const auto expected = allParameterIDs();

            expectEquals (static_cast<int> (actual.size()), params::numParameters);
            expect (actual == expected, "parameter ID set differs from SPEC §4");

            // no duplicate IDs
            expectEquals (static_cast<int> (actual.size()),
                          static_cast<int> (processor.getParameters().size()));
        }

        beginTest ("P3: every parameter display name is unique");
        {
            juce::StringArray names;
            juce::StringArray duplicates;
            for (auto* param : processor.getParameters())
            {
                const auto displayName = param->getName (128);
                if (names.contains (displayName))
                    duplicates.add (displayName);
                else
                    names.add (displayName);
            }

            expect (duplicates.isEmpty(),
                    "duplicate parameter names: " + duplicates.joinIntoString (", "));
            expectEquals (names.size(),
                          static_cast<int> (processor.getParameters().size()),
                          "parameter name count mismatch");
        }

        beginTest ("parameter types match SPEC (bool / choice / float)");
        {
            for (auto* param : processor.getParameters())
            {
                const auto id = paramID (param);

                const auto isBool   = std::any_of (std::begin (globalBoolParams), std::end (globalBoolParams),
                                                   [&] (const char* s) { return id == s; })
                                   || id.endsWith ("_sideband_enable")
                                   || id.endsWith ("_enable");   // T38 band master switch
                const auto isChoice = [] (const juce::String& i)
                {
                    if (i == "band_count" || i == "xover_slope" || i == "engine_version")
                        return true;
                    return i.endsWith ("_vib_wave");
                } (id);

                if (isBool)
                    expect (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr, id + " should be bool");
                else if (isChoice)
                    expect (dynamic_cast<juce::AudioParameterChoice*> (param) != nullptr, id + " should be choice");
                else
                    expect (dynamic_cast<juce::AudioParameterFloat*> (param) != nullptr, id + " should be float");
            }
        }

        beginTest ("global float ranges, defaults and steps match SPEC §4.2");
        {
            for (auto& expected : globalFloatParams)
            {
                auto* f = dynamic_cast<juce::AudioParameterFloat*> (findParam (processor, expected.id));
                expect (f != nullptr, juce::String (expected.id) + " missing");
                if (f == nullptr)
                    continue;

                auto& r = f->getNormalisableRange();
                expectWithinAbsoluteError (r.start,     expected.min,      1e-3f, juce::String (expected.id) + " min");
                expectWithinAbsoluteError (r.end,       expected.max,      1e-3f, juce::String (expected.id) + " max");
                expectWithinAbsoluteError (r.interval,  expected.interval, 1e-4f, juce::String (expected.id) + " interval");
                expectWithinAbsoluteError (r.convertFrom0to1 (paramDefaultValue (f)), expected.defaultValue, 1e-3f,
                                           juce::String (expected.id) + " default");
            }
        }

        beginTest ("per-band float ranges, defaults and steps match SPEC §4.3");
        {
            for (int band = 1; band <= params::numBands; ++band)
            {
                for (auto& expected : bandFloatParams)
                {
                    const auto id = params::makeBandParameterID (band, expected.id);
                    auto* f = dynamic_cast<juce::AudioParameterFloat*> (findParam (processor, id));
                    expect (f != nullptr, id + " missing");
                    if (f == nullptr)
                        continue;

                    auto& r = f->getNormalisableRange();
                    expectWithinAbsoluteError (r.start,     expected.min,      1e-3f, id + " min");
                    expectWithinAbsoluteError (r.end,       expected.max,      1e-3f, id + " max");
                    expectWithinAbsoluteError (r.interval,  expected.interval, 1e-4f, id + " interval");
                    expectWithinAbsoluteError (r.convertFrom0to1 (paramDefaultValue (f)), expected.defaultValue, 1e-3f, id + " default");
                }
            }
        }

        beginTest ("choice parameters match SPEC (list + default index)");
        {
            for (auto& expected : globalChoiceParams)
            {
                auto* c = dynamic_cast<juce::AudioParameterChoice*> (findParam (processor, expected.id));
                expect (c != nullptr, juce::String (expected.id) + " missing");
                if (c == nullptr)
                    continue;

                expect (c->choices == expected.choices, juce::String (expected.id) + " choices");
                expectEquals (c->getIndex(), expected.defaultIndex); // fresh instance = default

                const auto defaultIndex = static_cast<int> (std::lround (paramDefaultValue (c) * (expected.choices.size() - 1)));
                expectEquals (defaultIndex, expected.defaultIndex);
            }

            for (int band = 1; band <= params::numBands; ++band)
            {
                for (auto& expected : bandChoiceParams)
                {
                    const auto id = params::makeBandParameterID (band, expected.id);
                    auto* c = dynamic_cast<juce::AudioParameterChoice*> (findParam (processor, id));
                    expect (c != nullptr, id + " missing");
                    if (c == nullptr)
                        continue;

                    expect (c->choices == expected.choices, id + " choices");
                    expectEquals (c->getIndex(), expected.defaultIndex);

                    const auto defaultIndex = static_cast<int> (std::lround (paramDefaultValue (c) * (expected.choices.size() - 1)));
                    expectEquals (defaultIndex, expected.defaultIndex);
                }
            }
        }

        beginTest ("bool parameters default to off (SPEC §4.2)");
        {
            for (auto& id : globalBoolParams)
            {
                auto* b = dynamic_cast<juce::AudioParameterBool*> (findParam (processor, id));
                expect (b != nullptr, juce::String (id) + " missing");
                if (b != nullptr)
                    expect (! *b, juce::String (id) + " should default to false");
            }
        }

        beginTest ("log parameters map geometrically (0.5 <-> sqrt(min*max))");
        {
            auto checkLog = [&] (const char* id, float min, float max)
            {
                auto* f = dynamic_cast<juce::AudioParameterFloat*> (findParam (processor, id));
                expect (f != nullptr, juce::String (id) + " missing");
                if (f == nullptr)
                    return;

                auto& r = f->getNormalisableRange();
                const auto geometricMid = std::sqrt (min * max);

                expectWithinAbsoluteError (r.convertFrom0to1 (0.5f), geometricMid, geometricMid * 1e-3f, juce::String (id) + " 0.5->geo");
                expectWithinAbsoluteError (r.convertTo0to1 (geometricMid), 0.5f, 1e-4f, juce::String (id) + " geo->0.5");
                expectWithinAbsoluteError (r.convertFrom0to1 (0.0f), min, min * 1e-3f, juce::String (id) + " 0->min");
                expectWithinAbsoluteError (r.convertFrom0to1 (1.0f), max, max * 1e-3f, juce::String (id) + " 1->max");
            };

            for (auto& expected : globalFloatParams)
                if (expected.isLog)
                    checkLog (expected.id, expected.min, expected.max);

            for (int band = 1; band <= params::numBands; ++band)
                for (auto& expected : bandFloatParams)
                    if (expected.isLog)
                        checkLog (params::makeBandParameterID (band, expected.id).toRawUTF8(),
                                  expected.min, expected.max);
        }

        beginTest ("non-log float parameters map linearly");
        {
            for (auto& expected : globalFloatParams)
            {
                if (expected.isLog)
                    continue;

                auto* f = dynamic_cast<juce::AudioParameterFloat*> (findParam (processor, expected.id));
                expect (f != nullptr);
                if (f == nullptr)
                    continue;

                const auto linearMid = (expected.min + expected.max) * 0.5f;
                expectWithinAbsoluteError (f->getNormalisableRange().convertFrom0to1 (0.5f), linearMid, 1e-2f,
                                           juce::String (expected.id) + " should map linearly");
            }
        }

        beginTest ("sideband per-band float ranges, defaults and steps match SIDEBAND_SPEC §4.2");
        {
            for (int band = 1; band <= params::numBands; ++band)
            {
                for (auto& expected : bandSidebandFloatParams)
                {
                    const auto id = params::makeBandParameterID (band, expected.id);
                    auto* f = dynamic_cast<juce::AudioParameterFloat*> (findParam (processor, id));
                    expect (f != nullptr, id + " missing");
                    if (f == nullptr)
                        continue;

                    auto& r = f->getNormalisableRange();
                    expectWithinAbsoluteError (r.start,     expected.min,      1e-3f, id + " min");
                    expectWithinAbsoluteError (r.end,       expected.max,      1e-3f, id + " max");
                    expectWithinAbsoluteError (r.interval,  expected.interval, 1e-4f, id + " interval");
                    expectWithinAbsoluteError (r.convertFrom0to1 (paramDefaultValue (f)), expected.defaultValue, 1e-3f, id + " default");
                }
            }
        }

        beginTest ("sideband_lowcut maps geometrically (log range)");
        {
            for (int band = 1; band <= params::numBands; ++band)
            {
                const auto id = params::makeBandParameterID (band, "sideband_lowcut");
                auto* f = dynamic_cast<juce::AudioParameterFloat*> (findParam (processor, id));
                expect (f != nullptr, id + " missing");
                if (f == nullptr)
                    continue;

                auto& r = f->getNormalisableRange();
                const auto geometricMid = std::sqrt (100.0f * 8000.0f);
                expectWithinAbsoluteError (r.convertFrom0to1 (0.5f), geometricMid, geometricMid * 1e-3f, id + " 0.5->geo");
                expectWithinAbsoluteError (r.convertTo0to1 (geometricMid), 0.5f, 1e-4f, id + " geo->0.5");
            }
        }

        beginTest ("sideband_enable defaults on, engine_version defaults Legacy (C4/T62)");
        {
            for (int band = 1; band <= params::numBands; ++band)
            {
                const auto id = params::makeBandParameterID (band, "sideband_enable");
                auto* b = dynamic_cast<juce::AudioParameterBool*> (findParam (processor, id));
                expect (b != nullptr, id + " missing");
                if (b != nullptr)
                    expect (*b, id + " should default to on");
            }

            auto* engine = dynamic_cast<juce::AudioParameterChoice*> (findParam (processor, "engine_version"));
            expect (engine != nullptr, "engine_version missing");
            if (engine != nullptr)
                expectEquals (engine->getIndex(), 0); // T62: Sideband UI hidden -> Legacy default
        }

        beginTest ("legacy parameter prefix (43) keeps its registration order (C5/R19)");
        {
            // Expected registration order: the 10 globals (SPEC §4.2), then the
            // per-band loop 11 × b1/b2/b3 (9 original + T37 gain + T38 enable).
            // The prefix is 43 (the original v1 37 keep their relative order).
            // JUCE 9's VST3 wrapper hashes parameter ID strings by default, so
            // this order does not shift VST3 automation IDs; an AU/legacy-ID
            // build must keep version hints monotonic (gain/enable are v2).
            const char* globalOrder[] =
            {
                "bypass", "band_count", "xover1_freq", "xover2_freq", "xover_slope",
                "formant_enabled", "formant_shift", "formant_quefrency", "dry_wet", "output_trim",
            };
            const char* bandOrder[] =
            {
                "jitter_depth", "jitter_rate", "shimmer_depth", "shimmer_rate",
                "harmonic_drive", "harmonic_tone", "vib_wave", "vib_rate", "vib_depth",
                "gain", "enable",   // T37 gain / T38 enable (SPEC §4.3)
            };

            int index = 0;
            for (auto* g : globalOrder)
            {
                expectEquals (paramID (processor.getParameters()[index]), juce::String (g), "global order");
                ++index;
            }
            for (int band = 1; band <= params::numBands; ++band)
                for (auto* b : bandOrder)
                {
                    expectEquals (paramID (processor.getParameters()[index]),
                                  params::makeBandParameterID (band, b), "per-band order");
                    ++index;
                }

            expectEquals (index, 43); // legacy prefix length (T37 gain + T38 enable included)
            expectEquals (static_cast<int> (processor.getParameters().size()), 68);   // T49 +3 fog, T66 +3 Growl
        }

        beginTest ("new params carry versionHint 2, original v1 params carry 1");
        {
            const auto isNewParam = [] (const juce::String& id)
            {
                return id == "engine_version"
                    || id.endsWith ("_gain") || id.endsWith ("_enable")
                    || id.endsWith ("_sideband_enable")    || id.endsWith ("_sideband_depth")
                    || id.endsWith ("_sideband_fm_ratio")  || id.endsWith ("_sideband_roughness")
                    || id.endsWith ("_sideband_lowcut")    || id.endsWith ("_sideband_bright")
                    || id.endsWith ("_fog")            // T49
                    || id == "growl_amount" || id == "growl_rate" || id == "growl_tone"; // T66
            };

            for (auto* param : processor.getParameters())
            {
                auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (param);
                expect (withID != nullptr);
                if (withID == nullptr)
                    continue;

                const auto id = withID->getParameterID();
                expectEquals (withID->getVersionHint(), isNewParam (id) ? 2 : 1, id + " versionHint");
            }
        }
    }
};

//==============================================================================
//  Test 2: state persistence (real PluginProcessor code paths)
//==============================================================================
class ParameterPersistenceTest final : public juce::UnitTest
{
public:
    ParameterPersistenceTest() : juce::UnitTest ("ParameterPersistence") {}

    void runTest() override
    {
        beginTest ("XML round-trip preserves all 68 parameter values");
        {
            PluginProcessor source, restored;

            // Drive a representative subset of parameters away from defaults
            // (one of each kind; the round-trip check below covers all 65).
            setFloat  (source, "xover1_freq",        1234.5f);
            setFloat  (source, "xover2_freq",        9876.0f);
            setFloat  (source, "formant_shift",       -3.2f);
            setFloat  (source, "formant_quefrency",    2.75f);
            setFloat  (source, "dry_wet",             42.0f);
            setFloat  (source, "output_trim",          2.5f);
            setBool   (source, "bypass",              true);
            setBool   (source, "formant_enabled",     true);
            setChoice (source, "band_count",           1);   // "2"
            setChoice (source, "xover_slope",          2);   // "48"
            setFloat  (source, "b1_jitter_depth",     30.0f);
            setFloat  (source, "b1_vib_rate",        123.0f);
            setChoice (source, "b1_vib_wave",          3);   // Noise
            setFloat  (source, "b2_shimmer_depth",    55.0f);
            setFloat  (source, "b3_harmonic_tone",   7000.0f);
            setFloat  (source, "b1_fog",               63.0f);   // T49
            setFloat  (source, "growl_amount",          88.0f);  // T66
            setFloat  (source, "growl_rate",             0.24f);
            setFloat  (source, "growl_tone",            35.0f);
            // Sideband subset (SIDEBAND_SPEC §4)
            setBool   (source, "b1_sideband_enable",  false);
            setFloat  (source, "b2_sideband_depth",   75.0f);
            setFloat  (source, "b3_sideband_lowcut", 2500.0f);
            setFloat  (source, "b2_sideband_bright",  66.0f);   // T40
            setChoice (source, "engine_version",        1);   // Sideband

            juce::MemoryBlock data;
            source.getStateInformation (data);
            expect (data.getSize() > 0, "getStateInformation produced no data");

            restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));

            for (const auto& id : allParameterIDs())
            {
                auto valueA = source.apvts.getRawParameterValue (id)->load();
                auto valueB = restored.apvts.getRawParameterValue (id)->load();
                expectWithinAbsoluteError (valueA, valueB, 1e-5f, id + " differs after round-trip");
            }
        }

        beginTest ("missing parameters fall back to defaults (tolerant restore)");
        {
            PluginProcessor source, restored;

            setFloat (source, "formant_shift", -5.0f);
            setFloat (source, "dry_wet",       10.0f);
            setBool  (source, "bypass",        true);

            // Strip every parameter node except formant_shift from the state.
            // APVTS state: <PARAM id="..." value="..."/> children (tag is
            // always "PARAM"; the parameter ID lives in the "id" attribute).
            auto xml = source.apvts.copyState().createXml();
            expect (xml != nullptr);
            if (xml == nullptr)
                return;

            juce::Array<juce::XmlElement*> toRemove;
            for (auto* child : xml->getChildIterator())
                if (child->getStringAttribute ("id") != "formant_shift")
                    toRemove.add (child);
            for (auto* child : toRemove)
                xml->removeChildElement (child, true);

            juce::MemoryBlock data;
            juce::AudioProcessor::copyXmlToBinary (*xml, data);
            restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));

            // Present parameter restored...
            expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("formant_shift")->load(),
                                       source.apvts.getRawParameterValue ("formant_shift")->load(),
                                       1e-5f, "formant_shift should be restored");

            // ...missing parameters fall back to defaults. Note: JUCE 9
            // getRawParameterValue() yields denormalised (real-world) values,
            // so defaults must be converted back through the range.
            auto* dryWet = dynamic_cast<juce::AudioParameterFloat*> (restored.apvts.getParameter ("dry_wet"));
            expect (dryWet != nullptr);
            if (dryWet != nullptr)
            {
                const auto dryWetDefault = dryWet->getNormalisableRange().convertFrom0to1 (paramDefaultValue (dryWet));
                expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("dry_wet")->load(),
                                           dryWetDefault,
                                           1e-5f, "dry_wet should fall back to default");
            }

            expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("bypass")->load(),
                                       0.0f,
                                       1e-5f, "bypass should fall back to default (off)");
        }

        beginTest ("state version attribute is tolerated (missing / future)");
        {
            PluginProcessor source, restored;

            setFloat (source, "output_trim", 4.0f);

            // missing version attribute
            {
                auto xml = source.apvts.copyState().createXml();
                xml->removeAttribute ("version");

                juce::MemoryBlock data;
                juce::AudioProcessor::copyXmlToBinary (*xml, data);
                restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));

                expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("output_trim")->load(),
                                           source.apvts.getRawParameterValue ("output_trim")->load(),
                                           1e-5f, "state without version should load");
            }

            // future version attribute
            {
                auto xml = source.apvts.copyState().createXml();
                xml->setAttribute ("version", "99");

                juce::MemoryBlock data;
                juce::AudioProcessor::copyXmlToBinary (*xml, data);
                restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));

                expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("output_trim")->load(),
                                           source.apvts.getRawParameterValue ("output_trim")->load(),
                                           1e-5f, "future-version state should load tolerantly");
            }
        }

        beginTest ("v1 XML state migrates to Legacy engine (SB-U19 / DECISIONS R20)");
        {
            PluginProcessor source, restored;

            // A v1 state carries the 37 legacy parameters only. Build one from
            // a fresh (v2) state by stripping the 19 sideband params and
            // tagging it as version "1".
            setFloat  (source, "b3_harmonic_drive", 55.0f);
            setFloat  (source, "b3_harmonic_tone", 6500.0f);
            setChoice (source, "engine_version",     1); // adversarial: v2 says Sideband

            auto xml = source.apvts.copyState().createXml();
            expect (xml != nullptr);
            if (xml == nullptr)
                return;

            xml->setAttribute ("version", "1");

            juce::Array<juce::XmlElement*> toRemove;
            for (auto* child : xml->getChildIterator())
            {
                const auto id = child->getStringAttribute ("id");
                if (id == "engine_version"
                    || id.endsWith ("_sideband_enable")    || id.endsWith ("_sideband_depth")
                    || id.endsWith ("_sideband_fm_ratio")  || id.endsWith ("_sideband_roughness")
                    || id.endsWith ("_sideband_lowcut")    || id.endsWith ("_sideband_bright"))
                    toRemove.add (child);
            }
            for (auto* child : toRemove)
                xml->removeChildElement (child, true);

            juce::MemoryBlock data;
            juce::AudioProcessor::copyXmlToBinary (*xml, data);
            restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));

            // 37 legacy parameters restored bit-for-bit...
            expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("b3_harmonic_drive")->load(),
                                       55.0f, 1e-5f, "v1 b3_harmonic_drive should restore");
            expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("b3_harmonic_tone")->load(),
                                       6500.0f, 1e-3f, "v1 b3_harmonic_tone should restore (log)");

            // ...engine_version forced to Legacy (0), overriding the
            // adversarial Sideband value (REFACTOR_PLAN §4.2)...
            auto* engine = dynamic_cast<juce::AudioParameterChoice*> (
                restored.apvts.getParameter ("engine_version"));
            expect (engine != nullptr, "engine_version missing");
            if (engine != nullptr)
                expectEquals (engine->getIndex(), 0);

            // ...and the 19 new parameters land on their layout defaults.
            expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("b1_sideband_depth")->load(),
                                       0.0f, 1e-5f, "sideband_depth should fall back to default");
            expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("b1_sideband_lowcut")->load(),
                                       2000.0f, 1e-3f, "sideband_lowcut should fall back to default");
            expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("b1_sideband_fm_ratio")->load(),
                                       0.333f, 1e-3f, "sideband_fm_ratio should fall back to default");
            expectWithinAbsoluteError (restored.apvts.getRawParameterValue ("b1_sideband_bright")->load(),
                                       0.0f, 1e-5f, "sideband_bright should fall back to default (T40)");
        }

        beginTest ("invalid state data is rejected safely");
        {
            PluginProcessor restored;
            restored.setStateInformation (nullptr, 0); // must not crash

            const char* garbage = "not a binary xml blob";
            restored.setStateInformation (garbage, static_cast<int> (std::strlen (garbage))); // must not crash
            expect (true);
        }

        beginTest ("P3: an unknown state root leaves every parameter untouched");
        {
            PluginProcessor restored;
            auto* dryWet = restored.apvts.getParameter (params::IDs::dryWet);
            expect (dryWet != nullptr, "dry_wet parameter missing");
            if (dryWet == nullptr)
                return;

            dryWet->setValueNotifyingHost (0.25f);
            const auto before = dryWet->getValue();

            // A structurally valid XML blob with the wrong root must not be
            // applied (replaceState would otherwise reset everything).
            juce::XmlElement bogus ("NOT_A_STATE");
            bogus.setAttribute ("version", "2");
            juce::MemoryBlock data;
            juce::AudioProcessor::copyXmlToBinary (bogus, data);
            restored.setStateInformation (data.getData(), static_cast<int> (data.getSize()));

            expectWithinAbsoluteError (dryWet->getValue(), before, 1.0e-6f,
                                       "unknown state root changed a parameter");
        }

        beginTest ("P3: reported tail length covers the OLA ring");
        {
            PluginProcessor p;
            expectGreaterThan (p.getTailLengthSeconds(), 0.0,
                               "tail length must cover the formant/sideband rings");
        }
    }
};

ParameterLayoutTest parameterLayoutTest;
ParameterPersistenceTest parameterPersistenceTest;

} // namespace
