/*
    T66 — Settings latency text (moved out of the Master card).
*/

#include <juce_core/juce_core.h>

#include "dsp/FormantEngine.h"
#include "ui/TopBarComponent.h"

namespace
{

class SettingsLatencyTests final : public juce::UnitTest
{
public:
    SettingsLatencyTests() : juce::UnitTest ("SettingsLatency") {}

    void runTest() override
    {
        beginTest ("latency text uses samples + two-decimal milliseconds");
        {
            expectEquals (ui::TopBarComponent::formatLatencyText (0, 48000.0),
                          juce::String ("0 samples / 0.00 ms"));
            expectEquals (ui::TopBarComponent::formatLatencyText (2048, 48000.0),
                          juce::String ("2048 samples / 42.67 ms"));
            expectEquals (ui::TopBarComponent::formatLatencyText (1024, 96000.0),
                          juce::String ("1024 samples / 10.67 ms"));
            expectEquals (ui::TopBarComponent::formatLatencyText (2048, 0.0),
                          juce::String ("2048 samples / 0.00 ms"));
        }

        beginTest ("Settings frame-size label matches the Formant STFT (P2-2)");
        {
            expectEquals (ui::TopBarComponent::formantFrameSize,
                          dsp::FormantEngine::fftSize,
                          "Settings still shows the old 1024-point frame size");
        }

        beginTest ("T72 Settings dialog scales with the user UI size");
        {
            expect (ui::TopBarComponent::settingsSizeForScale (1.0f) == juce::Point<int> (340, 496));
            expect (ui::TopBarComponent::settingsSizeForScale (1.25f) == juce::Point<int> (425, 620));
            expect (ui::TopBarComponent::settingsSizeForScale (1.5f) == juce::Point<int> (510, 744));
            // Clamped to the editor's supported range (50–250 %).
            expect (ui::TopBarComponent::settingsSizeForScale (9.0f) == juce::Point<int> (850, 1240));
        }
    }
};

SettingsLatencyTests settingsLatencyTests;

} // namespace
