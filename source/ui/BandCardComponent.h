#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "params/ParameterIDs.h"
#include "ui/DopaminePalette.h"

namespace ui
{

// T18/T38: per-band effect card (SPEC §3 F2–F5, F12; REFACTOR_PLAN §6.1).
//
// T62/T63 (user ruling): the cards live in the editor's right column. The
// Sideband Modulation UI is hidden for now (the DSP, parameters and factory
// presets remain); the Band On switch lives in the tab strip, so the card
// itself starts directly with its first section:
//   Band Effects (subtitle)
//   Jit Depth | Jit Rate | Rough
//   Shim Depth | Shim Rate | (empty)
//   Vibration (subtitle)
//   [wave combo, full row]
//   Depth | Rate | (empty)
//
// Grey-out rule: band off -> every visible control greys out; band_count < N
// -> the tab does not exist (editor manages tabs).
//
// Every control gets its parameter ID as componentID, so tests can locate
// them with findChildWithID (no extra public interface — task card).
class BandCardComponent final : public juce::Component,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::AsyncUpdater
{
public:
    BandCardComponent (juce::AudioProcessorValueTreeState& apvts, int bandIndex);
    ~BandCardComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (Knob&, const juce::String& paramID, const juce::String& displayName,
                  dopamine::Role);
    void addSubtitle (juce::Label&, const juce::String& text, dopamine::Role);
    void layKnobRow (juce::Rectangle<int>& row, std::initializer_list<Knob*> knobs,
                     int columns);
    void updateGreyOut() noexcept;   // paint-time sync (band enable)

    juce::AudioProcessorValueTreeState& apvts;
    const int bandIndex;

    // Subtitle-row backgrounds (painted as full rows).
    juce::Rectangle<int> jsSubRect, vibSubRect;
    // Refreshed by updateGreyOut() (every paint) so the painted subtitle
    // rows dim together with the controls when the band is off.
    bool bandOnState = true;

    // Subtitles.
    juce::Label jsTitle;    // "Band Effects"
    juce::Label vibTitle;   // "Vibration"

    // Band Effects knobs + whole-band Rough (T49; parameter ID stays _fog).
    Knob jitDepth, jitRate, shimDepth, shimRate, bandFog;

    // Vibration knobs.
    Knob vibDepth, vibRate;

    // 1 combo: vibration waveform (full row).
    juce::Label vibWaveLabel;
    juce::ComboBox vibWaveCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> vibWaveAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandCardComponent)
};

} // namespace ui
