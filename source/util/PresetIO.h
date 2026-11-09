#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class PluginProcessor;

namespace util
{

/*
    PresetIO (T42): custom preset files (*.nvpreset) — SPEC §3 F9.

    FORMAT (plain UTF-8 XML, human-readable / diffable):

        <NeoVoiDrivePreset formatVersion="1" stateVersion="2" name="My Preset">
          <PARAMETERS version="2">
            <PARAM id="..." value="..."/>          (APVTS::copyState() XML)
            ...
          </PARAMETERS>
        </NeoVoiDrivePreset>

    - the inner PARAMETERS tree is the same data a DAW session stores, so the
      loader reuses PluginProcessor::applyStateXml() (v1 -> Legacy migration,
      tolerant handling of missing/unknown params);
    - exports force `bypass` to 0 IN THE FILE (a preset must never load
      bypassed); the live state is not modified; imports apply the same
      policy to raw session-state XML;
    - imports accept the wrapper root (recommended) or a raw PARAMETERS root
      (e.g. an exported session state file);
    - a `formatVersion` newer than this build is rejected with an error.

    THREADING: every file operation must run on the message thread (the UI's
    Preset menu). Nothing here is called from the audio thread.
*/
class PresetIO final
{
public:
    static constexpr const char* fileExtension  = ".nvpreset";
    static constexpr const char* rootTag        = "NeoVoiDrivePreset";
    static constexpr int         formatVersion  = 1;
    static constexpr juce::int64 maxFileBytes   = 1024 * 1024; // 1 MB sanity cap

    /** Forces the .nvpreset extension (case-insensitive; replaces any other). */
    static juce::File withPresetExtension (const juce::File& file);

    /** Display/preset name = file name without the extension ("Untitled" if empty). */
    static juce::String nameFromFile (const juce::File& file);

    /** Builds the preset XML for the APVTS' current state (no file IO). */
    static std::unique_ptr<juce::XmlElement> createPresetXml (
        juce::AudioProcessorValueTreeState& apvts, const juce::String& name);

    /** Writes the current state as a preset file. Returns false + error on
        failure (the live state is never modified). */
    static bool save (juce::AudioProcessorValueTreeState& apvts,
                      const juce::File& file,
                      const juce::String& name,
                      juce::String& error);

    /** Parses + applies a preset file through PluginProcessor::applyStateXml().
        Returns false + error (and leaves the parameters untouched) on failure. */
    static bool load (PluginProcessor& processor, const juce::File& file, juce::String& error);

    /** Sorted *.nvpreset files in `directory` (missing folder -> empty). */
    static juce::Array<juce::File> listPresetsIn (const juce::File& directory);

    /** Sorted user presets in <userAppData>/NeoVoiDrive/Presets. */
    static juce::Array<juce::File> listUserPresets();

    JUCE_DECLARE_NON_COPYABLE (PresetIO)

private:
    PresetIO() = delete;
};

} // namespace util
