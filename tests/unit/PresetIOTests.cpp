/*
    NeoVoiDrive T42 tests — PresetIO (custom *.nvpreset import / export).

    Acceptance (SPEC §3 F9, DECISIONS R38):
      1. an exported preset restores every parameter in a fresh instance
         (except bypass / debug-solo, which are forced off IN THE FILE);
      2. the file is readable UTF-8 XML with the documented root/attributes;
      3. a v1 state inside a preset is migrated (engine_version -> Legacy);
      4. malformed / wrong-root / newer-format / oversized files are rejected
         and leave the current parameters untouched;
      5. the user library listing filters *.nvpreset and sorts by name;
      6. the raw PARAMETERS root (session-state XML) is accepted.

    File IO stays inside the test working directory (build/ or the repo root,
    both writable in the sandbox) and is removed before/after the run.
*/

#include <juce_core/juce_core.h>

#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "util/PresetIO.h"

namespace
{

//==============================================================================
juce::String idOf (juce::AudioProcessorParameter* p)
{
    if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
        return withID->paramID;
    return {};
}

void setFloat (PluginProcessor& p, const char* id, float value)
{
    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p.apvts.getParameter (id)))
        *f = value;
}

void setBool (PluginProcessor& p, const char* id, bool value)
{
    if (auto* b = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter (id)))
        *b = value;
}

void setChoice (PluginProcessor& p, const char* id, int index)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)))
        *c = index;
}

float rawValue (PluginProcessor& p, const char* id)
{
    auto* v = p.apvts.getRawParameterValue (id);
    return v != nullptr ? v->load() : -9999.0f;
}

//==============================================================================
class PresetIOTests final : public juce::UnitTest
{
public:
    PresetIOTests() : juce::UnitTest ("PresetIO") {}

    void runTest() override
    {
        root = juce::File::getCurrentWorkingDirectory().getChildFile ("nvd_preset_io_test");
        root.deleteRecursively();
        root.createDirectory();

        runRoundTrip();
        runFileFormat();
        runForcedFlags();
        runV1Migration();
        runRawStateRoot();
        runRejections();
        runListing();
        runHelpers();

        root.deleteRecursively();
    }

private:
    juce::File root;

    juce::File testFile (const juce::String& fileName) const
    {
        return root.getChildFile (fileName);
    }

    //==========================================================================
    void runRoundTrip()
    {
        beginTest ("export -> import restores every parameter in a fresh instance");
        {
            PluginProcessor a;
            PluginProcessor b;

            setChoice (a, "band_count", 2);          // 3 bands
            setChoice (a, "engine_version", 1);      // Sideband
            setChoice (a, "xover_slope", 4);         // 60 dB/oct
            setFloat  (a, "xover1_freq", 321.0f);
            setFloat  (a, "xover2_freq", 4321.0f);
            setBool   (a, "formant_enabled", true);
            setFloat  (a, "formant_shift", -4.5f);
            setFloat  (a, "formant_quefrency", 2.25f);
            setFloat  (a, "dry_wet", 88.0f);
            setFloat  (a, "output_trim", -3.5f);
            setFloat  (a, "b1_sideband_depth", 70.0f);
            setFloat  (a, "b2_sideband_bright", 66.0f);
            setFloat  (a, "b3_sideband_roughness", 85.0f);
            setFloat  (a, "b2_gain", 6.0f);
            setBool   (a, "b3_enable", false);
            setFloat  (a, "b1_harmonic_drive", 42.0f);   // legacy params too
            setChoice (a, "b2_vib_wave", 3);
            setFloat  (a, "b3_fog", 58.0f);              // T49 whole-band fog

            const auto file = testFile ("Round Trip.nvpreset");
            juce::String error;
            expect (util::PresetIO::save (a.apvts, file, "Round Trip", error),
                    "save failed: " + error);
            expect (file.existsAsFile(), "preset file was not created");

            expect (util::PresetIO::load (b, file, error), "load failed: " + error);

            int compared = 0;
            for (auto* pa : a.getParameters())
            {
                const auto id = idOf (pa);
                if (id.isEmpty())
                    continue;

                auto* pb = b.apvts.getParameter (id);
                expect (pb != nullptr, id + " missing in the loaded instance");
                if (pb == nullptr)
                    continue;

                expectWithinAbsoluteError (pa->getValue(), pb->getValue(), 1e-5f,
                                           id + " differs after the preset round-trip");
                ++compared;
            }

            // All 68 parameters must be compared.
            expectEquals (compared, static_cast<int> (a.getParameters().size()));
            expect (compared >= 68, "unexpectedly small parameter count");
        }
    }

    //==========================================================================
    void runFileFormat()
    {
        beginTest ("exported file is readable XML with the documented root/attributes");
        {
            PluginProcessor p;
            setFloat (p, "dry_wet", 77.0f);

            const auto file = testFile ("No Extension"); // extension forced below
            juce::String error;
            expect (util::PresetIO::save (p.apvts, file, "Format Test", error), "save failed: " + error);

            const auto expected = util::PresetIO::withPresetExtension (file);
            expect (expected.existsAsFile(), "save did not add the .nvpreset extension");

            const auto xml = juce::XmlDocument::parse (expected);
            expect (xml != nullptr, "exported preset is not valid XML");
            if (xml != nullptr)
            {
                expect (xml->hasTagName (util::PresetIO::rootTag), "wrong root tag");
                expectEquals (xml->getIntAttribute ("formatVersion", -1), util::PresetIO::formatVersion);
                expectEquals (xml->getIntAttribute ("stateVersion", -1), 2);
                expectEquals (xml->getStringAttribute ("name"), juce::String ("Format Test"));

                auto* state = xml->getFirstChildElement();
                expect (state != nullptr && state->hasTagName ("PARAMETERS"),
                        "missing PARAMETERS child");
                expectEquals (state != nullptr ? state->getIntAttribute ("version", -1) : -1, 2);

                // A real parameter is present with its PHYSICAL value (JUCE 9
                // APVTS ValueTrees store denormalised values).
                bool foundDryWet = false;
                for (auto* param : state->getChildIterator())
                    if (param->getStringAttribute ("id") == "dry_wet")
                    {
                        foundDryWet = true;
                        expectWithinAbsoluteError (static_cast<float> (param->getDoubleAttribute ("value")),
                                                   77.0f, 1e-4f);
                    }
                expect (foundDryWet, "dry_wet missing from the exported state");
            }
        }
    }

    //==========================================================================
    void runForcedFlags()
    {
        beginTest ("export forces bypass off in the file (live state untouched)");
        {
            PluginProcessor p;
            setBool (p, "bypass", true);

            const auto file = testFile ("Forced Flags.nvpreset");
            juce::String error;
            expect (util::PresetIO::save (p.apvts, file, "Forced", error), "save failed: " + error);

            const auto xml = juce::XmlDocument::parse (file);
            expect (xml != nullptr);
            if (xml != nullptr)
            {
                auto* state = xml->getFirstChildElement();
                expect (state != nullptr);
                if (state != nullptr)
                {
                    for (auto* param : state->getChildIterator())
                    {
                        const auto id = param->getStringAttribute ("id");
                        if (id == "bypass")
                            expectWithinAbsoluteError (static_cast<float> (param->getDoubleAttribute ("value")),
                                                       0.0f, 1e-6f, id + " not forced off in the preset");
                    }
                }
            }

            // The live state keeps the user's flags.
            expect (rawValue (p, "bypass") > 0.5f, "live bypass was modified by export");
        }
    }

    //==========================================================================
    void runV1Migration()
    {
        beginTest ("v1 preset state migrates: engine_version forced to Legacy");
        {
            const auto file = testFile ("V1.nvpreset");
            const juce::String xml =
                "<NeoVoiDrivePreset formatVersion=\"1\" stateVersion=\"1\" name=\"V1\">"
                "  <PARAMETERS version=\"1\">"
                "    <PARAM id=\"engine_version\" value=\"1\"/>"
                "    <PARAM id=\"b1_jitter_depth\" value=\"600\"/>"
                "  </PARAMETERS>"
                "</NeoVoiDrivePreset>";
            expect (file.replaceWithText (xml), "could not write the v1 preset");

            PluginProcessor p;   // fresh v2 instance (engine defaults Legacy, T62)
            setChoice (p, "engine_version", 1);

            juce::String error;
            expect (util::PresetIO::load (p, file, error), "v1 load failed: " + error);

            auto* engine = dynamic_cast<juce::AudioParameterChoice*> (
                p.apvts.getParameter (params::IDs::engineVersion));
            expect (engine != nullptr);
            if (engine != nullptr)
                expectEquals (engine->getIndex(), 0); // Legacy (R20)

            expectWithinAbsoluteError (rawValue (p, "b1_jitter_depth"), 600.0f, 1e-3f);

            // Parameters absent from the v1 state are untouched / defaults.
            expectWithinAbsoluteError (rawValue (p, "dry_wet"), 100.0f, 1e-3f);
        }
    }

    //==========================================================================
    void runRawStateRoot()
    {
        beginTest ("raw PARAMETERS root (session-state XML) is accepted");
        {
            PluginProcessor a;
            setFloat (a, "dry_wet", 33.0f);
            setBool (a, "bypass", true);

            auto stateXml = a.apvts.copyState().createXml();
            expect (stateXml != nullptr);
            if (stateXml == nullptr)
                return;
            stateXml->setAttribute ("version", "2");

            const auto file = testFile ("raw_state.xml");
            expect (stateXml->writeTo (file), "could not write the raw state file");

            PluginProcessor b;
            juce::String error;
            expect (util::PresetIO::load (b, file, error), "raw state load failed: " + error);
            expectWithinAbsoluteError (rawValue (b, "dry_wet"), 33.0f, 1e-3f);
            // Import applies the same transport-flag policy as export.
            expectEquals (rawValue (b, "bypass"), 0.0f);
        }
    }

    //==========================================================================
    void runRejections()
    {
        beginTest ("malformed / wrong-root / newer / oversized files are rejected safely");
        {
            PluginProcessor p;
            setFloat (p, "dry_wet", 42.0f);   // sentinel

            const auto expectRejected = [this, &p] (const juce::String& fileName,
                                                     const juce::String& content,
                                                     const juce::String& what)
            {
                const auto file = testFile (fileName);
                expect (file.replaceWithText (content), "could not write " + fileName);

                juce::String error;
                expect (! util::PresetIO::load (p, file, error), what + " was accepted");
                expect (error.isNotEmpty(), what + " produced no error message");
                expectWithinAbsoluteError (rawValue (p, "dry_wet"), 42.0f, 1e-5f,
                                           what + " modified the parameters");
            };

            expectRejected ("garbage.nvpreset", "this is not xml", "non-XML");
            expectRejected ("wrongroot.nvpreset", "<Foo/>", "wrong root");
            expectRejected ("nostate.nvpreset",
                            "<NeoVoiDrivePreset formatVersion=\"1\"/>", "missing PARAMETERS");
            expectRejected ("newer.nvpreset",
                            "<NeoVoiDrivePreset formatVersion=\"99\"><PARAMETERS version=\"2\"/></NeoVoiDrivePreset>",
                            "newer format");

            // Oversized file (> 1 MB): rejected before parsing.
            {
                const auto file = testFile ("huge.nvpreset");
                juce::MemoryBlock big (static_cast<size_t> (util::PresetIO::maxFileBytes + 16), true);
                expect (file.replaceWithData (big.getData(), big.getSize()), "could not write the oversized file");
                juce::String error;
                expect (! util::PresetIO::load (p, file, error), "oversized file was accepted");
                expect (error.isNotEmpty(), "oversized file produced no error message");
            }

            // Missing file.
            {
                juce::String error;
                expect (! util::PresetIO::load (p, testFile ("missing.nvpreset"), error),
                        "missing file was accepted");
                expect (error.isNotEmpty(), "missing file produced no error message");
            }
        }
    }

    //==========================================================================
    void runListing()
    {
        beginTest ("user library listing filters *.nvpreset and sorts by name");
        {
            const auto dir = root.getChildFile ("listing");
            dir.createDirectory();

            const auto empty = util::PresetIO::listPresetsIn (dir);
            expectEquals (empty.size(), 0);

            expect (dir.getChildFile ("z.nvpreset").replaceWithText ("<x/>"));
            expect (dir.getChildFile ("a.nvpreset").replaceWithText ("<x/>"));
            expect (dir.getChildFile ("B.NVPRESET").replaceWithText ("<x/>"));
            expect (dir.getChildFile ("notes.txt").replaceWithText ("hello"));
            expect (dir.getChildFile ("sub.nvpreset").createDirectory());

            const auto files = util::PresetIO::listPresetsIn (dir);
            expectEquals (files.size(), 3);
            if (files.size() == 3)
            {
                expectEquals (files[0].getFileName(), juce::String ("a.nvpreset"));
                expectEquals (files[1].getFileName(), juce::String ("B.NVPRESET"));
                expectEquals (files[2].getFileName(), juce::String ("z.nvpreset"));
                expectEquals (util::PresetIO::nameFromFile (files[0]), juce::String ("a"));
            }

            expectEquals (util::PresetIO::listPresetsIn (root.getChildFile ("nope")).size(), 0);
        }
    }

    //==========================================================================
    void runHelpers()
    {
        beginTest ("extension / name helpers");
        {
            expectEquals (util::PresetIO::withPresetExtension (testFile ("foo")).getFileName(),
                          juce::String ("foo.nvpreset"));
            expectEquals (util::PresetIO::withPresetExtension (testFile ("foo.XML")).getFileName(),
                          juce::String ("foo.nvpreset"));
            expectEquals (util::PresetIO::withPresetExtension (testFile ("foo.nvpreset")).getFileName(),
                          juce::String ("foo.nvpreset"));
            expect (util::PresetIO::withPresetExtension (juce::File()) == juce::File(),
                    "empty file must stay empty");

            expectEquals (util::PresetIO::nameFromFile (testFile ("My Preset.nvpreset")),
                          juce::String ("My Preset"));
            expectEquals (util::PresetIO::nameFromFile (testFile ("My Preset")),
                          juce::String ("My Preset"));
            expectEquals (util::PresetIO::nameFromFile (juce::File()),
                          juce::String ("Untitled"));
            juce::ignoreUnused (util::PresetIO::listUserPresets()); // must not throw
        }
    }
};

PresetIOTests presetIOTests;

} // namespace
