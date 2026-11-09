#pragma once

/*
    APVTS parameter layout factory (T02).

    Mechanically generates all 68 parameters from SPEC §4 (10 global +
    3 × 11 legacy band + 3 × 6 sideband + engine_version + 3 × fog).
    Ranges, defaults, steps and log mappings must match SPEC exactly — the
    unit tests in tests/unit/ParameterLayoutTests.cpp assert every value.
*/

#include <juce_audio_processors/juce_audio_processors.h>

namespace params
{

class ParameterLayout
{
public:
    /** Builds the complete APVTS parameter layout (68 parameters, SPEC §4). */
    static juce::AudioProcessorValueTreeState::ParameterLayout create();
};

} // namespace params
