#pragma once

#include <juce_core/juce_core.h>

/*
    UserData (T42): one source of truth for the per-user NeoVoiDrive data
    directory. Message-thread file IO only; these are pure path builders.

        <userApplicationDataDirectory>/NeoVoiDrive/
            NeoVoiDrive.settings    (T41: uiScale)
            Presets/                (T42: custom *.nvpreset files)

    Windows: %APPDATA%\NeoVoiDrive.
*/
namespace util::UserData
{

inline juce::File getAppDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("NeoVoiDrive");
}

inline juce::File getSettingsFile()
{
    return getAppDirectory().getChildFile ("NeoVoiDrive.settings");
}

inline juce::File getPresetDirectory()
{
    return getAppDirectory().getChildFile ("Presets");
}

} // namespace util::UserData
