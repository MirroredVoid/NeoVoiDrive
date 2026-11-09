/*
    NeoVoiDrive T16 tests — PresetBank + TopBarComponent.

    Acceptance (TASKS.md T16):
      1. >= 8 factory presets with unique, non-empty names;
      2. loading a preset sets every listed parameter to its defined value;
      3. presets differ from each other (at least one parameter);
      4. every preset value lies within its parameter range (SPEC §4);
      5. bypass button <-> parameter sync is bidirectional (TopBar);
      6. repeated loading is stable (no crash, values unchanged).
    Settings dialog / About stay manual (UI).
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "ui/TopBarComponent.h"
#include "util/PresetBank.h"

#include <map>
#include <set>

namespace
{

//==============================================================================
class PresetBankTest final : public juce::UnitTest
{
public:
    PresetBankTest() : juce::UnitTest ("PresetBank") {}

    void runTest() override
    {
        beginTest ("preset names are unique and non-empty");
        {
            expectGreaterThan (util::PresetBank::numPresets, 0);
            expectEquals (util::PresetBank::numPresets, 17);
            std::set<juce::String> seen;
            for (int i = 0; i < util::PresetBank::numPresets; ++i)
            {
                const auto presetName = util::PresetBank::getPresetName (i);
                expect (presetName.isNotEmpty(), "preset " + juce::String (i) + " has an empty name");
                expect (! seen.contains (presetName), "duplicate preset name " + presetName);
                seen.insert (presetName);
            }

            expectEquals (juce::String (util::PresetBank::getPresetName (11)),
                          juce::String ("Growl 1"), "Growl 1 name");
            expectEquals (juce::String (util::PresetBank::getPresetName (12)),
                          juce::String ("Growl 2"), "Growl 2 name");
            expectEquals (juce::String (util::PresetBank::getPresetName (13)),
                          juce::String ("Growl 3"), "Growl 3 name");
            expectEquals (juce::String (util::PresetBank::getPresetName (14)),
                          juce::String ("Anger 1"), "Anger 1 name");
            expectEquals (juce::String (util::PresetBank::getPresetName (15)),
                          juce::String ("Anger 2"), "Anger 2 name");
            expectEquals (juce::String (util::PresetBank::getPresetName (16)),
                          juce::String ("Anger 3"), "Anger 3 name");
        }

        beginTest ("factory presets are grouped: Voice Drive, then Sound Effects");
        {
            // T69: the menu iterates the bank twice (one pass per category), so
            // the grouping contract is the category table itself.
            juce::StringArray voiceDrive, soundEffects;
            for (int i = 0; i < util::PresetBank::numPresets; ++i)
            {
                const auto presetCategory = util::PresetBank::getPresetCategory (i);
                const auto presetName = juce::String (util::PresetBank::getPresetName (i));
                if (presetCategory == util::PresetBank::Category::voiceDrive)
                    voiceDrive.add (presetName);
                else
                    soundEffects.add (presetName);
            }

            expectEquals (voiceDrive.joinIntoString (", "),
                          juce::String ("Growl 1, Growl 2, Growl 3, Anger 1, Anger 2, Anger 3"),
                          "Voice Drive group order");
            expectEquals (soundEffects.size(), 11, "Sound Effects group size");
            expectEquals (juce::String (util::PresetBank::getCategoryName (
                              util::PresetBank::Category::voiceDrive)),
                          juce::String ("Voice Drive"));
            expectEquals (juce::String (util::PresetBank::getCategoryName (
                              util::PresetBank::Category::soundEffects)),
                          juce::String ("Sound Effects"));
        }

        beginTest ("loading a preset applies every listed parameter");
        {
            PluginProcessor processor;

            // JUCE 9: getRawParameterValue() returns the PHYSICAL value.
            util::PresetBank::loadPreset (processor.apvts, 0);

            // Clean Voice sets formant_shift to 0 and dry_wet to 100.
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("formant_shift")->load(),
                                       0.0f, 1e-4f, "Clean Voice formant_shift");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("dry_wet")->load(),
                                       100.0f, 1e-4f, "Clean Voice dry_wet");

            // Bright Exciter sets band values.
            util::PresetBank::loadPreset (processor.apvts, 6);
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_harmonic_drive")->load(),
                                       55.0f, 1e-4f, "Bright Exciter b3_harmonic_drive");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_harmonic_tone")->load(),
                                       6500.0f, 1e-3f, "Bright Exciter b3_harmonic_tone (log)");

            // Vintage Vib sets a choice parameter.
            util::PresetBank::loadPreset (processor.apvts, 7);
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b1_vib_wave")->load(),
                                       0.0f, 1e-4f, "Vintage Vib b1_vib_wave");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b1_vib_depth")->load(),
                                       18.0f, 1e-4f, "Vintage Vib b1_vib_depth");

            // Growl 2 (index 12): the original T61 preset — Legacy engine,
            // 128 Hz b2 sidebands, b3 fog/gain, no global growl.
            util::PresetBank::loadPreset (processor.apvts, 12);
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("engine_version")->load(),
                                       0.0f, 1e-4f, "Growl 2 engine_version");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("xover1_freq")->load(),
                                       1000.0f, 1e-3f, "Growl 2 xover1_freq");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b2_jitter_rate")->load(),
                                       320.0f, 1e-3f, "Growl 2 b2_jitter_rate");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b2_jitter_depth")->load(),
                                       375.0f, 1e-4f, "Growl 2 b2_jitter_depth");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_fog")->load(),
                                       30.0f, 1e-4f, "Growl 2 b3_fog");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_gain")->load(),
                                       6.0f, 1e-4f, "Growl 2 b3_gain");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("growl_amount")->load(),
                                       0.0f, 1e-4f, "Growl 2 growl_amount");

            // Growl 1 / 3: lighter and heavier tiers of the same character.
            util::PresetBank::loadPreset (processor.apvts, 11);
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b2_jitter_depth")->load(),
                                       220.0f, 1e-4f, "Growl 1 b2_jitter_depth");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_fog")->load(),
                                       18.0f, 1e-4f, "Growl 1 b3_fog");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_gain")->load(),
                                       4.0f, 1e-4f, "Growl 1 b3_gain");

            util::PresetBank::loadPreset (processor.apvts, 13);
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b2_jitter_depth")->load(),
                                       500.0f, 1e-4f, "Growl 3 b2_jitter_depth");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_fog")->load(),
                                       45.0f, 1e-4f, "Growl 3 b3_fog");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_gain")->load(),
                                       7.0f, 1e-4f, "Growl 3 b3_gain");

            // Anger 2 (index 15): the accepted N3 global growl.
            util::PresetBank::loadPreset (processor.apvts, 15);
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("engine_version")->load(),
                                       0.0f, 1e-4f, "Anger 2 engine_version");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("band_count")->load(),
                                       2.0f, 1e-4f, "Anger 2 band_count");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b2_jitter_rate")->load(),
                                       300.0f, 1e-4f, "Anger 2 b2_jitter_rate");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_fog")->load(),
                                       10.0f, 1e-4f, "Anger 2 b3_fog");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("growl_amount")->load(),
                                       100.0f, 1e-4f, "Anger 2 growl_amount");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("growl_rate")->load(),
                                       0.20f, 1e-4f, "Anger 2 growl_rate");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("growl_tone")->load(),
                                       0.0f, 1e-4f, "Anger 2 growl_tone");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("output_trim")->load(),
                                       2.0f, 1e-4f, "Anger 2 output_trim");

            // Anger 1 / 3: lighter (Amount 60) and heavier (slower/darker,
            // more bed roughness) tiers.
            util::PresetBank::loadPreset (processor.apvts, 14);
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("growl_amount")->load(),
                                       60.0f, 1e-4f, "Anger 1 growl_amount");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b2_jitter_depth")->load(),
                                       60.0f, 1e-4f, "Anger 1 b2_jitter_depth");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("output_trim")->load(),
                                       1.0f, 1e-4f, "Anger 1 output_trim");

            util::PresetBank::loadPreset (processor.apvts, 16);
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("growl_amount")->load(),
                                       100.0f, 1e-4f, "Anger 3 growl_amount");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("growl_rate")->load(),
                                       0.15f, 1e-4f, "Anger 3 growl_rate");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("growl_tone")->load(),
                                       25.0f, 1e-4f, "Anger 3 growl_tone");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b2_jitter_depth")->load(),
                                       180.0f, 1e-4f, "Anger 3 b2_jitter_depth");
            expectWithinAbsoluteError (processor.apvts.getRawParameterValue ("b3_fog")->load(),
                                       22.0f, 1e-4f, "Anger 3 b3_fog");
        }

        beginTest ("Growl/Anger tiers are ordered light < medium < heavy");
        {
            PluginProcessor processor;
            const auto value = [&processor] (const char* id)
            {
                return processor.apvts.getRawParameterValue (id)->load();
            };

            // Growl: the same Legacy character, deeper jitter and fog upward.
            util::PresetBank::loadPreset (processor.apvts, 11);
            const auto g1Jitter = value ("b2_jitter_depth");
            const auto g1Fog    = value ("b3_fog");
            util::PresetBank::loadPreset (processor.apvts, 12);
            const auto g2Jitter = value ("b2_jitter_depth");
            const auto g2Fog    = value ("b3_fog");
            util::PresetBank::loadPreset (processor.apvts, 13);
            expect (g1Jitter < g2Jitter && g2Jitter < value ("b2_jitter_depth"),
                    "Growl b2 jitter depth is not ordered light < medium < heavy");
            expect (g1Fog < g2Fog && g2Fog < value ("b3_fog"),
                    "Growl b3 Rough is not ordered light < medium < heavy");

            // Anger: global Amount up, bed roughness up, heavy is slower and
            // darker (Amount is already 100 for medium).
            util::PresetBank::loadPreset (processor.apvts, 14);
            const auto a1Amount = value ("growl_amount");
            const auto a1Jitter = value ("b2_jitter_depth");
            util::PresetBank::loadPreset (processor.apvts, 15);
            const auto a2Amount = value ("growl_amount");
            const auto a2Jitter = value ("b2_jitter_depth");
            const auto a2Rate   = value ("growl_rate");
            const auto a2Tone   = value ("growl_tone");
            util::PresetBank::loadPreset (processor.apvts, 16);
            expect (a1Amount < a2Amount, "Anger Amount does not increase");
            expect (a1Jitter < a2Jitter && a2Jitter < value ("b2_jitter_depth"),
                    "Anger bed jitter is not ordered light < medium < heavy");
            expect (a2Amount <= value ("growl_amount"), "Anger 3 Amount lower than Anger 2");
            expect (value ("growl_rate") < a2Rate, "Anger 3 Rate is not slower");
            expect (value ("growl_tone") > a2Tone, "Anger 3 Tone is not darker");
        }

        beginTest ("factory presets are complete snapshots (defaults first; band_count covers used bands)");
        {
            PluginProcessor processor;

            // Dirty the state, then load Clean Voice: Bypass, debug solo,
            // effect depths and band_count must all fall back to defaults.
            if (auto* b = dynamic_cast<juce::AudioParameterBool*> (
                    processor.apvts.getParameter ("bypass")))
                *b = true;
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (
                    processor.apvts.getParameter ("b1_jitter_depth")))
                *f = 50.0f;
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (
                    processor.apvts.getParameter ("b1_sideband_depth")))
                *f = 100.0f;
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (
                    processor.apvts.getParameter ("band_count")))
                *c = 2;

            util::PresetBank::loadPreset (processor.apvts, 0); // Clean Voice

            expectEquals (processor.apvts.getRawParameterValue ("bypass")->load(), 0.0f,
                          "factory preset must clear Bypass");
            expectEquals (processor.apvts.getRawParameterValue ("b1_jitter_depth")->load(), 0.0f,
                          "unlisted jitter must return to its default");
            expectEquals (processor.apvts.getRawParameterValue ("b1_sideband_depth")->load(), 0.0f,
                          "unlisted sideband depth must return to its default");
            expectEquals (processor.apvts.getRawParameterValue ("band_count")->load(), 0.0f,
                          "unlisted band_count must return to its default");
            expectEquals (processor.apvts.getRawParameterValue ("b1_enable")->load(), 1.0f,
                          "band enable default is on");

            // Any preset that writes b2/b3 parameters must declare enough bands;
            // otherwise those parameters are silently ignored on a fresh instance.
            for (int i = 0; i < util::PresetBank::numPresets; ++i)
            {
                util::PresetBank::loadPreset (processor.apvts, i);
                const auto* preset = util::PresetBank::getPreset (i);
                expect (preset != nullptr);
                if (preset == nullptr)
                    continue;

                const int bands = juce::jlimit (1, 3, static_cast<int> (
                    std::lround (processor.apvts.getRawParameterValue ("band_count")->load())) + 1);
                for (int k = 0; k < preset->numParams; ++k)
                {
                    const juce::String id (preset->params[k].id);
                    if (id.startsWith ("b1_") || id.startsWith ("b2_") || id.startsWith ("b3_"))
                    {
                        const int bandNumber = id[1] - '0';
                        expect (bandNumber >= 1 && bandNumber <= bands,
                                juce::String (preset->name) + " uses " + id
                                    + " but band_count only enables " + juce::String (bands));
                    }
                }

                expectEquals (processor.apvts.getRawParameterValue ("bypass")->load(), 0.0f,
                              juce::String (preset->name) + " must not load bypassed");
            }
        }

        beginTest ("every preset parameter ID exists and presets differ pairwise");
        {
            PluginProcessor processor;

            // a) Every ID referenced by a preset must exist in the APVTS
            //    (otherwise applyParam silently ignores it).
            for (int i = 0; i < util::PresetBank::numPresets; ++i)
            {
                const auto* preset = util::PresetBank::getPreset (i);
                expect (preset != nullptr, "preset " + juce::String (i) + " missing");
                if (preset == nullptr)
                    return;
                for (int k = 0; k < preset->numParams; ++k)
                {
                    expect (processor.apvts.getParameter (preset->params[k].id) != nullptr,
                            juce::String (preset->params[k].id) + " (preset "
                            + juce::String (i) + ") is not a parameter");
                }
            }

            // b) Any two presets differ on at least one parameter they touch.
            for (int i = 0; i < util::PresetBank::numPresets; ++i)
            {
                for (int j = i + 1; j < util::PresetBank::numPresets; ++j)
                {
                    util::PresetBank::loadPreset (processor.apvts, i);
                    std::map<juce::String, float> vi;
                    collectPresetValues (processor.apvts, i, vi);

                    util::PresetBank::loadPreset (processor.apvts, j);
                    std::map<juce::String, float> vj;
                    collectPresetValues (processor.apvts, j, vj);

                    bool same = true;
                    for (const auto& [id, value] : vi)
                    {
                        if (vj.count (id) > 0 && std::abs (value - vj[id]) > 1e-5f)
                        {
                            same = false;
                            break;
                        }
                    }
                    for (const auto& [id, value] : vj)
                    {
                        if (vi.count (id) == 0 && std::abs (value) > 1e-5f)
                        {
                            same = false;
                            break;
                        }
                    }
                    expect (! same, "presets " + juce::String (i) + " and "
                                    + juce::String (j) + " are identical");
                }
            }
        }

        beginTest ("all preset values stay finite after loading (ranges clamped)");
        {
            PluginProcessor processor;
            bool allFinite = true;
            for (int i = 0; i < util::PresetBank::numPresets; ++i)
            {
                util::PresetBank::loadPreset (processor.apvts, i);
                for (const auto& id : allParameterIDs())
                {
                    const auto v = processor.apvts.getRawParameterValue (id)->load();
                    if (! std::isfinite (v))
                    {
                        allFinite = false;
                        break;
                    }
                }
            }
            expect (allFinite, "a preset value became non-finite");
        }

        beginTest ("formant presets enable the formant path (M2)");
        {
            // Warm Voice / Pitch Up / Pitch Down shape the formant — loading
            // them must flip formant_enabled on, or the engine stays off and
            // the user hears nothing (review M2).
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            for (int i = 0; i < util::PresetBank::numPresets; ++i)
            {
                const auto* preset = util::PresetBank::getPreset (i);
                if (preset == nullptr)
                    continue;

                bool shiftsFormant = false;
                for (int k = 0; k < preset->numParams; ++k)
                {
                    if (juce::String (preset->params[k].id) == "formant_shift"
                        && std::abs (preset->params[k].value) > 0.001f)
                        shiftsFormant = true;
                }

                if (shiftsFormant)
                {
                    bool hasEnable = false;
                    for (int k = 0; k < preset->numParams; ++k)
                        if (juce::String (preset->params[k].id) == "formant_enabled"
                            && preset->params[k].value > 0.5f)
                            hasEnable = true;
                    expect (hasEnable,
                            juce::String ("preset '") + juce::String (preset->name)
                                + "' shifts the formant but does not enable it");
                }
                else
                {
                    // M2 (reverse): every non-formant preset must explicitly
                    // turn the formant path OFF, so switching from Pitch Up
                    // to e.g. Radio Voice doesn't leave it running.
                    bool hasDisable = false;
                    for (int k = 0; k < preset->numParams; ++k)
                        if (juce::String (preset->params[k].id) == "formant_enabled"
                            && preset->params[k].value < 0.5f)
                            hasDisable = true;
                    expect (hasDisable,
                            juce::String ("preset '") + juce::String (preset->name)
                                + "' does not disable the formant path");
                }
            }
        }

        beginTest ("every preset declares engine_version (own intent), load-order independent (T30/T35)");
        {
            PluginProcessor processor;

            // a) Data level: every factory preset must explicitly list
            //    engine_version. Without this, the engine choice depends on
            //    the state before loading (migration trap, IMPACT_ANALYSIS
            //    易破坏点 #8 / REFACTOR_PLAN §4.4). Intent: the 8 voice
            //    presets (0..7) stay Legacy; the 3 Sideband showcase presets
            //    (8..10) use Sideband; the Growl/Anger families (11..16) are
            //    Legacy.
            for (int i = 0; i < util::PresetBank::numPresets; ++i)
            {
                const auto* preset = util::PresetBank::getPreset (i);
                expect (preset != nullptr, "preset " + juce::String (i) + " missing");
                if (preset == nullptr)
                    return;

                const bool sidebandPreset = (i >= 8 && i <= 10);
                const float declaredEngine = sidebandPreset ? 1.0f : 0.0f;
                bool declaresEngine = false;
                for (int k = 0; k < preset->numParams; ++k)
                {
                    if (juce::String (preset->params[k].id) == "engine_version")
                    {
                        expectWithinAbsoluteError (preset->params[k].value, declaredEngine, 1e-6f,
                                                   juce::String (preset->name) + " engine_version value");
                        declaresEngine = true;
                    }
                }
                expect (declaresEngine,
                        juce::String ("preset '") + juce::String (preset->name)
                            + "' does not declare engine_version (load-order trap)");
            }

            // b) Behaviour: loading any preset lands engine_version on the
            //    preset's declared value (Legacy for 0..7 and 11..16, Sideband
            //    for 8..10) even after an adversarial prior state.
            auto* engine = dynamic_cast<juce::AudioParameterChoice*> (
                processor.apvts.getParameter ("engine_version"));
            expect (engine != nullptr, "engine_version missing");
            if (engine == nullptr)
                return;

            for (int i = 0; i < util::PresetBank::numPresets; ++i)
            {
                const bool sidebandPreset = (i >= 8 && i <= 10);
                *engine = sidebandPreset ? 0 : 1; // force the opposite engine first
                util::PresetBank::loadPreset (processor.apvts, i);
                const int declared = sidebandPreset ? 1 : 0;
                expectEquals (engine->getIndex(), declared,
                              juce::String ("preset ") + juce::String (i)
                                  + " left engine_version on the wrong engine (load-order dependent)");
            }
        }

        beginTest ("repeated loading is stable");
        {
            PluginProcessor processor;
            util::PresetBank::loadPreset (processor.apvts, 2);
            std::map<juce::String, float> before;
            for (const auto& id : allParameterIDs())
                before[id] = processor.apvts.getRawParameterValue (id)->load();

            for (int round = 0; round < 3; ++round)
                util::PresetBank::loadPreset (processor.apvts, 2);

            bool stable = true;
            for (const auto& id : allParameterIDs())
            {
                if (processor.apvts.getRawParameterValue (id)->load() != before[id])
                {
                    stable = false;
                    break;
                }
            }
            expect (stable, "repeated loads changed parameter values");
        }

        beginTest ("bypass button and parameter stay in sync both ways");
        {
            PluginProcessor processor;
            ui::TopBarComponent topBar (processor);
            topBar.setSize (520, 48);

            auto* param = dynamic_cast<juce::AudioParameterBool*> (processor.apvts.getParameter ("bypass"));

            // Default: off both sides.
            expect (! param->get(), "bypass parameter should default to off");
                    // Button -> parameter.
            auto* button = dynamic_cast<juce::Button*> (topBar.findChildWithID ("bypassButton"));
            expect (button != nullptr, "bypass button not found");
            if (button == nullptr)
                return;

            button->setToggleState (true, juce::sendNotification);
            expect (param->get(), "button on did not set the parameter");

            button->setToggleState (false, juce::sendNotification);
            expect (! param->get(), "button off did not clear the parameter");

            // Parameter -> button.
            *param = true;
            expect (button->getToggleState(), "parameter on did not toggle the button");

            *param = false;
            expect (! button->getToggleState(), "parameter off did not untoggle the button");
        }
    }

private:
    static void collectPresetValues (juce::AudioProcessorValueTreeState& apvts, int index,
                                     std::map<juce::String, float>& out)
    {
        const auto* preset = util::PresetBank::getPreset (index);
        for (int k = 0; k < preset->numParams; ++k)
        {
            const auto* raw = apvts.getRawParameterValue (preset->params[k].id);
            if (raw != nullptr)
                out[preset->params[k].id] = raw->load();
        }
    }

    static const juce::StringArray& allParameterIDs()
    {
        static const juce::StringArray ids = [] ()
        {
            juce::StringArray a;
            for (auto* param : { "bypass", "band_count", "xover1_freq", "xover2_freq", "xover_slope",
                                 "formant_enabled", "formant_shift", "formant_quefrency", "dry_wet", "output_trim",
                                 "engine_version", "growl_amount", "growl_rate", "growl_tone" })
                a.add (param);
            for (int b = 1; b <= 3; ++b)
                for (auto* base : { "jitter_depth", "jitter_rate", "shimmer_depth", "shimmer_rate",
                                    "harmonic_drive", "harmonic_tone", "vib_wave", "vib_rate", "vib_depth",
                                    "sideband_enable", "sideband_depth", "sideband_fm_ratio",
                                    "sideband_roughness", "sideband_lowcut", "sideband_bright" })
                    a.add (params::makeBandParameterID (b, base));
            return a;
        }();
        return ids;
    }

};

PresetBankTest presetBankTest;

} // namespace
