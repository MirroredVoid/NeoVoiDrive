/*
    NeoVoiDrive unit tests — smoke cases (T20).

    This file currently contains only harness smoke tests. Cases for T02/T04 and
    the DSP tasks (T05-T12) mount here as they land; new files under tests/unit/
    are picked up automatically by the CMake GLOB in tests/CMakeLists.txt.
*/

#include <juce_core/juce_core.h>

//==============================================================================
//  Smoke test: the harness itself — toolchain, C++ standard and JUCE version.
//==============================================================================
class HarnessSmokeTest final : public juce::UnitTest
{
public:
    HarnessSmokeTest() : juce::UnitTest ("HarnessSmoke") {}

    void runTest() override
    {
        beginTest ("JUCE version is 9.0.0");
        expect (JUCE_MAJOR_VERSION == 9 && JUCE_MINOR_VERSION == 0,
                "expected JUCE 9.0.0, got " + juce::String (JUCE_MAJOR_VERSION) + "."
                + juce::String (JUCE_MINOR_VERSION));

        beginTest ("C++20 is enabled");
        expect (JUCE_CXX20_IS_AVAILABLE,
                "expected C++20 or newer (JUCE_CXX20_IS_AVAILABLE was false)");

        beginTest ("basic expect machinery");
        expect (1 + 1 == 2);
        expectEquals (juce::String ("neo") + "voi", juce::String ("neovoi"));
        expectGreaterThan (42, 0);
    }
};

static HarnessSmokeTest harnessSmokeTest;
