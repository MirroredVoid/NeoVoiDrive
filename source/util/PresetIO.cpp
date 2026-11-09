#include "util/PresetIO.h"

#include <algorithm>

#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "util/UserData.h"

namespace util
{

//==============================================================================
juce::File PresetIO::withPresetExtension (const juce::File& file)
{
    if (file == juce::File())
        return {};

    if (file.hasFileExtension (fileExtension)) // case-insensitive in JUCE
        return file;

    return file.withFileExtension (fileExtension);
}

juce::String PresetIO::nameFromFile (const juce::File& file)
{
    const auto name = file.getFileNameWithoutExtension().trim();
    return name.isNotEmpty() ? name : juce::String ("Untitled");
}

//==============================================================================
std::unique_ptr<juce::XmlElement> PresetIO::createPresetXml (
    juce::AudioProcessorValueTreeState& apvts, const juce::String& name)
{
    auto stateXml = apvts.copyState().createXml();
    if (stateXml == nullptr)
        return {};

    // T42/T46: a preset must never restore the transport-only Bypass flag.
    // Force it to 0 in the FILE (the live APVTS state is untouched).
    for (auto* param : stateXml->getChildIterator())
    {
        const auto id = param->getStringAttribute ("id");
        if (id == params::IDs::bypass)
            param->setAttribute ("value", 0.0);
    }

    stateXml->setAttribute ("version", "2"); // session state version (T30)

    auto root = std::make_unique<juce::XmlElement> (rootTag);
    root->setAttribute ("formatVersion", formatVersion);
    root->setAttribute ("stateVersion", "2");
    root->setAttribute ("name", name.isNotEmpty() ? name : juce::String ("Untitled"));
    root->addChildElement (stateXml.release());
    return root;
}

//==============================================================================
bool PresetIO::save (juce::AudioProcessorValueTreeState& apvts,
                     const juce::File& file,
                     const juce::String& name,
                     juce::String& error)
{
    error.clear();

    const auto target = withPresetExtension (file);
    if (target == juce::File())
    {
        error = "Invalid preset file path.";
        return false;
    }

    auto parent = target.getParentDirectory();
    if (const auto r = parent.createDirectory(); r.failed())
    {
        error = "Cannot create the preset folder:\n" + r.getErrorMessage();
        return false;
    }

    auto xml = createPresetXml (apvts, name.isNotEmpty() ? name : nameFromFile (target));
    if (xml == nullptr)
    {
        error = "Could not serialise the current state.";
        return false;
    }

    if (! xml->writeTo (target))
    {
        error = "Cannot write the preset file:\n" + target.getFullPathName();
        return false;
    }

    return true;
}

//==============================================================================
bool PresetIO::load (PluginProcessor& processor, const juce::File& file, juce::String& error)
{
    error.clear();

    if (! file.existsAsFile())
    {
        error = "Preset file not found:\n" + file.getFullPathName();
        return false;
    }

    if (file.getSize() > maxFileBytes)
    {
        error = "Preset file is too large (limit 1 MB).";
        return false;
    }

    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
    {
        error = "Not a valid XML file:\n" + file.getFileName();
        return false;
    }

    juce::XmlElement* state = nullptr;

    if (xml->hasTagName (rootTag))
    {
        const int fmt = xml->getIntAttribute ("formatVersion", formatVersion);
        if (fmt > formatVersion)
        {
            error = "This preset uses a newer format (v" + juce::String (fmt)
                    + "). Please update NeoVoiDrive.";
            return false;
        }

        for (auto* child : xml->getChildIterator())
            if (child->hasTagName ("PARAMETERS"))
            {
                state = child;
                break;
            }

        if (state == nullptr)
        {
            error = "Preset contains no parameter state.";
            return false;
        }
    }
    else if (xml->hasTagName ("PARAMETERS"))
    {
        // Raw session-state XML (e.g. a saved DAW state file): accepted so
        // users can reuse existing backups as presets.
        state = xml.get();
    }
    else
    {
        error = "Not a NeoVoiDrive preset (unexpected root <" + xml->getTagName() + ">).";
        return false;
    }

    // A preset must never restore the transport-only flags, whether it came
    // from our own wrapper or from a raw session-state XML. Keep the same
    // policy as export (the live state is only modified by this import).
    for (auto* param : state->getChildIterator())
    {
        const auto id = param->getStringAttribute ("id");
        if (id == params::IDs::bypass)
            param->setAttribute ("value", 0.0);
    }

    // Shared with session restore: APVTS replaceState + v1 -> Legacy migration.
    // Message thread only (replaceState locks).
    processor.applyStateXml (*state);
    return true;
}

//==============================================================================
juce::Array<juce::File> PresetIO::listPresetsIn (const juce::File& directory)
{
    juce::Array<juce::File> found;
    if (! directory.isDirectory())
        return found;

    directory.findChildFiles (found, juce::File::findFiles, false);

    // Keep only *.nvpreset (case-insensitive) and sort by file name so the
    // menu order is stable regardless of the file system's enumeration order.
    juce::Array<juce::File> files;
    for (const auto& f : found)
        if (f.getFileName().endsWithIgnoreCase (fileExtension))
            files.add (f);

    std::sort (files.begin(), files.end(),
               [] (const juce::File& a, const juce::File& b)
               {
                   return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
               });

    return files;
}

juce::Array<juce::File> PresetIO::listUserPresets()
{
    return listPresetsIn (UserData::getPresetDirectory());
}

} // namespace util
