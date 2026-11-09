#pragma once

#include "ui/NeoLookAndFeel.h"

/*
    TopBarComponent (T16): Logo / Bypass / Preset menu / Settings dialog
    (SPEC §3 F8/F9/F11, §4.2).

      - Bypass: TextButton bound to the `bypass` parameter via a
        ButtonAttachment — both directions stay in sync automatically.
      - Preset: PopupMenu listing the factory presets (util::PresetBank), the
        user preset library (T42: <userAppData>/NeoVoiDrive/Presets/*.nvpreset)
        and the custom preset actions Export / Import / Show Preset Folder
        (util::PresetIO).
      - Settings: quefrency slider (instant + saved via the APVTS state),
        framesize slot (T68 fixed 2048, disabled), UI Size (T41) and About
        info dialog. The Engine selector is hidden while the Sideband UI is
        dormant (T62); engine_version stays a normal APVTS parameter.
*/

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{

class TopBarComponent final : public juce::Component
{
public:
    explicit TopBarComponent (PluginProcessor& processor);
    ~TopBarComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    //==========================================================================
    //  T41: UI size. The editor supplies both accessors before the Settings
    //  dialog can be opened (message thread only). Missing callbacks simply
    //  leave the UI-size row at its default (100 %).
    std::function<float()>     getUserScale;
    std::function<void (float)> setUserScale;

    /** T66: read-only latency text shared with the Settings dialog/tests. */
    static juce::String formatLatencyText (int samples, double sampleRate);

    /** T68/audit P2-2: the Formant STFT size shown in Settings. Must stay in
        sync with dsp::FormantEngine::fftSize (guarded by a unit test). */
    static constexpr int formantFrameSize = 2048;

    /** T72: Settings dialog pixel size at a user scale (design 340 x 496). */
    static juce::Point<int> settingsSizeForScale (float scale) noexcept;

private:
    //==========================================================================
    class SettingsDialog final : public juce::Component,
                                 private juce::Timer
    {
    public:
        // T41: userScale is the editor's current UI scale (1.0 = the compact
        // default); onScaleChange is invoked immediately when the combo moves
        // (the editor resizes itself on the message thread).
        // T66: the latency provider is read-only and updates on a timer.
        SettingsDialog (juce::AudioProcessorValueTreeState& apvts,
                        float userScale,
                        std::function<void (float)> onScaleChange,
                        std::function<int()> getLatencySamples,
                        double sampleRate);

        /** Synchronously unbinds the APVTS attachments. Call before the
            processor (and its apvts) is destroyed while the dialog may still
            be open: the DialogWindow itself is deleted asynchronously by the
            modal manager, which can run after the apvts is gone. */
        void detachFromApvts() noexcept
        {
            quefrencyAttachment.reset();
        }

        void resized() override;
        void paint (juce::Graphics&) override;

        /** T72: dialog pixels for a user scale (design canvas 340 x 496). */
        static juce::Point<int> sizeForScale (float scale) noexcept;

    private:
        // T72: the whole dialog is authored at 340 x 496 and rendered through
        // an AffineTransform, so every label/knob/combo scales together with
        // the main UI. Popup menus leave this window and are scaled by theme.
        class Canvas final : public juce::Component
        {
        public:
            explicit Canvas (SettingsDialog& ownerRef) : owner (ownerRef) {}

            void paint (juce::Graphics& g) override { owner.paintCanvas (g); }
            void resized() override { owner.layoutCanvas(); }

        private:
            SettingsDialog& owner;
        };

        void applyScale (float scale);
        void layoutCanvas();
        void paintCanvas (juce::Graphics&);

        static constexpr int designWidth = 340;
        static constexpr int designHeight = 496;
        float uiScale = 1.0f;
        Canvas canvas { *this };

        // M4: top-level popups don't inherit the editor's root L&F — own one.
        ui::NeoLookAndFeel theme { false }; // popup: must NOT hijack the global default L&F

        juce::AudioProcessorValueTreeState& apvts;

        // Batch 2: the dialog follows the main UI's section-strip language.
        juce::Label formantStrip;
        juce::Label monitorStrip;
        juce::Label interfaceStrip;
        juce::Label aboutStrip;
        juce::Rectangle<int> formantStripRect, monitorStripRect, interfaceStripRect, aboutStripRect;

        juce::Label quefrencyLabel;
        juce::Slider quefrencySlider { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> quefrencyAttachment;

        juce::Label framesizeLabel;
        juce::ComboBox framesizeCombo;

        // T41: UI size (user scale, 75–150 %; 100 % = the compact default).
        juce::Label uiScaleLabel;
        juce::ComboBox uiScaleCombo;
        std::function<void (float)> onUiScaleChange;

        juce::Label aboutLabel;

        // T66: latency moved out of the Master card into Settings.
        juce::Label latencyLabel, latencyValue;
        std::function<int()> getLatencySamples;
        double sampleRate = 48000.0;

        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDialog)
    };

    //==========================================================================
    void showPresetMenu();
    void showSettings();

    // T42: custom preset import/export. All of these run on the message
    // thread; file IO never touches the audio thread.
    void exportPresetDialog();
    void importPresetDialog();
    void openPresetFolder();
    void loadUserPreset (const juce::File& file);
    static void showPresetError (const juce::String& title, const juce::String& message,
                                 juce::Component* parent);

    PluginProcessor& processor;

    // T42: one native file dialog at a time; replacing the chooser cancels the
    // previous dialog and its callback guards with a SafePointer.
    std::unique_ptr<juce::FileChooser> presetFileChooser;

    juce::Label logoLabel;
    juce::TextButton bypassButton;
    juce::TextButton presetButton;
    juce::TextButton settingsButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Tracks the open Settings window so editor teardown can close it
    // (the dialog holds an apvts reference; see ~TopBarComponent).
    juce::Component::SafePointer<juce::Component> settingsContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBarComponent)
};

} // namespace ui
