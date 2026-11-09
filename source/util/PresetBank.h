#pragma once

/*
    PresetBank (T16): factory presets (SPEC §4 F9 / ARCHITECTURE §2).

    Factory presets (17 after T67), defined as physical-value parameter tables
    (id + value) and applied to the APVTS with the typed operator=
    (Float / Bool / Choice). All values are within the SPEC §4 ranges — the
    unit tests assert every preset value.

    Threading contract: loadPreset() must only be called from the message
    thread (the preset menu lives in the UI; ARCHITECTURE §2 "预设仅经消息
    线程加载"). It is not RT-safe by design.
*/

#include <juce_audio_processors/juce_audio_processors.h>

namespace util
{

class PresetBank
{
public:
    static constexpr int numPresets = 17;   // T35 +2 Sideband; T40 +1 Steel Core; T67 Growl 1/2/3 + Anger 1/2/3

    /** T69: preset menu grouping. */
    enum class Category
    {
        voiceDrive,   // Growl 1/2/3 + Anger 1/2/3
        soundEffects  // the remaining factory presets
    };

    /** Display name of preset `index` (0 .. numPresets-1). */
    static juce::StringRef getPresetName (int index) noexcept;

    /** T69: group of factory preset `index` (out-of-range -> soundEffects). */
    static Category getPresetCategory (int index) noexcept;

    /** T69: menu section title for a category. */
    static juce::StringRef getCategoryName (Category category) noexcept;

    /** Applies preset `index` to the APVTS from a clean default state: all
        parameters are reset to their defaults first, then the preset's values
        are applied. Message thread only. */
    static void loadPreset (juce::AudioProcessorValueTreeState& apvts, int index) noexcept;

    struct PresetParam
    {
        const char* id;
        float value; // physical value; bool = 0/1, choice = index
    };

    struct Preset
    {
        const char* name;
        Category category;
        const PresetParam* params;
        int numParams;
    };

    /** Direct access to the preset tables (tests / tooling). */
    static const Preset* getPreset (int index) noexcept;
};

} // namespace util
