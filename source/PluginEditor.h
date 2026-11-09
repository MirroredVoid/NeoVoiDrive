#pragma once

#include "PluginProcessor.h"
#include "ui/BandCardComponent.h"
#include "ui/BottomBarComponent.h"
#include "ui/CrossoverDisplay.h"
#include "ui/NeoLookAndFeel.h"
#include "ui/TopBarComponent.h"

#include <array>

//==============================================================================
/**
    NeoVoiDrive plugin editor (skeleton, T01; TopBar assembled in T16).

    The light modern theme (NeoLookAndFeel, T15) is installed as the default
    look-and-feel for the whole editor hierarchy. The remaining zones
    (CrossoverDisplay / BandCards / BottomBar) arrive with T17–T19.

    T41 (UI size): the whole editor is authored on a fixed design canvas
    (designWidth × designHeight). All zones live inside ONE `Content` child
    that is laid out in design coordinates; the editor applies a uniform
    AffineTransform::scale() to that child and sizes itself to the scaled
    canvas. This keeps every existing pixel metric/font intact while making
    the window resizable through Settings:

      - T62/T63: the canvas is now 1200×720 with a full-width header and a
        754 / 406 px two-column main area (left ~65 %: spectrum/crossover,
        right: Band tabs + Master). Each tab contains its own Band On switch
        left of the tab text. Only Master was shortened; the Band card keeps
        its 400 px content height. 100 % = design × defaultScaleBase =
        1020×612;
      - the Settings "UI Size" control sets a USER scale in [0.75, 1.50]
        (1.0 = default);
      - the user scale is persisted per user (not per project) in
        <userAppData>/NeoVoiDrive/NeoVoiDrive.settings — message thread only;
      - if the host changes the editor width, `resized()` re-derives the
        scale from the width (height follows the design aspect), so the UI
        always stays proportional.
*/
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    //  T41: UI scaling (design canvas + user scale)
    //==============================================================================
    static constexpr int   designWidth      = 1200;  // T63 two-column canvas
    static constexpr int   designHeight     = 720;
    static constexpr float defaultScaleBase = 0.85f; // 100 % == 1020 x 612
    static constexpr float minUserScale     = 0.75f;
    static constexpr float maxUserScale     = 1.50f;

    /** Editor size (px) for a user scale — the Settings control and the tests
        use this so the rounding rules live in one place. */
    static juce::Point<int> sizeForUserScale (float userScale) noexcept;

    /** Applies a user scale (clamped). `persist = false` is for tests/tooling
        (no settings-file write). Message thread only. */
    void setUiScale (float userScale, bool persist = true);

    /** Current user scale (1.0 = default; not the AffineTransform factor). */
    float getUserScale() const noexcept { return userScale; }

    /** Test/tooling hook: run the pending deferred UI update synchronously
        (S1 — the real GUI does this on the message thread). */
    void flushPendingAsyncUpdate() { handleUpdateNowIfNeeded(); }

private:
    //==========================================================================
    /** T41: the whole UI canvas. Children are laid out in design coordinates
        and the editor scales this component as a whole. */
    class Content final : public juce::Component
    {
    public:
        void paint (juce::Graphics&) override;
        void resized() override;

        PluginEditor* owner = nullptr;
    };

    /** band_count changes -> the tab set follows (T38) / grey-out (F12, R12). */
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override; // S1: GUI work stays on the message thread
    void updateBandTabs();

    /** T63: a transparent wrapper gives the per-tab Band On switch breathing
        room from the tab edge and the "Band N" text. */
    class BandEnableSwitch final : public juce::Component
    {
    public:
        BandEnableSwitch()
        {
            setInterceptsMouseClicks (false, true);
            addAndMakeVisible (toggle);
        }

        void resized() override
        {
            toggle.setBounds (getLocalBounds().reduced (8, 3));
        }

        juce::ToggleButton toggle;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandEnableSwitch)
    };

    // T41: layout/paint of the design canvas (called from Content).
    void layoutContent (juce::Component& canvas);
    void paintContent (juce::Graphics&, const juce::Component& canvas);

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    PluginProcessor& processorRef;

    // T15: the theme — installed as the default L&F so every child component
    // (sliders, combos, popups, …) picks it up.
    ui::NeoLookAndFeel neoLookAndFeel;

    // T41: scaled design canvas. Declared before the zones so it is destroyed
    // after them (children deregister from their parent on destruction).
    Content content;

    // T16: top zone — Logo / Bypass / Preset / Settings.
    ui::TopBarComponent topBar;

    // T17: spectrum window + crossover drag / band-count / slope controls.
    ui::CrossoverDisplay crossoverDisplay;

    // T18/T38/T63: three per-band effect cards inside tabs; the tab count
    // follows band_count (cards are members — tabs are added with
    // deleteWhenRemoved = false so clearTabs() never deletes them). Each tab
    // gets an owned Band On switch (extra component, before the tab text).
    juce::TabbedComponent bandTabs { juce::TabbedButtonBar::TabsAtTop };
    ui::BandCardComponent bandCard1, bandCard2, bandCard3;

    std::array<juce::ToggleButton*, 3> bandEnableToggles {};
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 3>
        bandEnableAttachments;

    // T19: bottom bar — formant / dry-wet / trim / meters / latency.
    ui::BottomBarComponent bottomBar;

    // T41: USER UI scale, 1.0 = the new compact default.
    float userScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
