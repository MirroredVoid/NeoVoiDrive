#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "ui/DopaminePalette.h"
#include "ui/MeterSource.h"

#include <functional>

namespace ui
{

// T19/T62/T66: right-column "Master" card. It now contains two tabbed pages:
// Growl (left/default) and Master. The bottom 64 px is L/R metering only;
// latency moved to Settings.
class BottomBarComponent final : public juce::Component,
                                  private juce::Timer,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::AsyncUpdater
{
public:
    BottomBarComponent (juce::AudioProcessorValueTreeState& apvts,
                        ui::MeterSource& meter,
                        PluginProcessor& processor);
    ~BottomBarComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Samples -> milliseconds at the current sample rate (pure, tested). */
    static double latencyMs (int samples, double sampleRate) noexcept;

    /** Test hook: run any queued async update now (headless tests have no
        message loop). Same pattern as PluginEditor::flushPendingAsyncUpdate. */
    void flushPendingAsyncUpdate() { handleUpdateNowIfNeeded(); }

    // T66 test hooks
    juce::TabbedComponent& getMasterTabs() noexcept { return masterTabs; }
    juce::Rectangle<float> getMeterBarBounds (int index) const noexcept
    {
        return index == 0 ? meterL : meterR;
    }

private:
    // APVTS listener (S1 pattern: audio thread -> AsyncUpdater).
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { refreshFormantKnobEnabled(); }
    void refreshFormantKnobEnabled() noexcept;

    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // Transparent tab page: lays out its knobs via a callback.
    class Page final : public juce::Component
    {
    public:
        std::function<void()> layoutCallback;

        void resized() override
        {
            if (layoutCallback != nullptr)
                layoutCallback();
        }

        void paint (juce::Graphics&) override {}
    };

    void addKnob (Knob&, juce::Component& parent, const juce::String& paramID,
                  const juce::String& displayName, dopamine::Role);
    void layoutMasterPage();
    void layoutGrowlPage();
    void timerCallback() override;

    juce::AudioProcessorValueTreeState& apvts;
    ui::MeterSource& meter;
    PluginProcessor& processor;

    // Pages/knobs are declared before the TabbedComponent so the tabs are
    // destroyed first; the TabbedComponent holds raw content pointers.
    Page growlPage, masterPage;

    Knob formantKnob, dryWetKnob, trimKnob;
    Knob amountKnob, rateKnob, toneKnob;
    juce::ToggleButton formantToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> formantToggleAttachment;

    juce::TabbedComponent masterTabs { juce::TabbedButtonBar::TabsAtTop };

    // Meter bars (card-local coords) and 30 Hz peak state.
    juce::Rectangle<float> meterL, meterR;
    float leftPeak = 0.0f, rightPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomBarComponent)
};

} // namespace ui
