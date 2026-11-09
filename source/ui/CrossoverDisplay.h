#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

#include "params/ParameterIDs.h"
#include "ui/SpectrumSource.h"

namespace ui
{

// T17: real-time spectrum window (log axis 40–18000 Hz) with draggable
// crossover lines, band-count and slope selectors (SPEC §3 F1 / F9).
//
// Reader side of SpectrumSource: a 60 Hz timer consumes the newest FFT block
// (2048 points, Hann window) and repaints. Dragging a crossover line writes
// the xover parameters through the APVTS with xover1 < xover2 enforced at
// the drag layer (the DSP side already smooths parameter steps, T10/T13).
//
// All buffers are preallocated; the timer callback runs on the message
// thread, never on the audio thread.
class CrossoverDisplay final : public juce::Component, private juce::Timer
{
public:
    CrossoverDisplay (juce::AudioProcessorValueTreeState& apvts,
                      ui::SpectrumSource& spectrum,
                      ui::SpectrumSource& spectrumOut);
    ~CrossoverDisplay() override;

    //==========================================================================
    // ---- pure helpers (unit-tested without a component instance) ----
    //==========================================================================
    static constexpr float minFreqHz = 40.0f;
    static constexpr float maxFreqHz = 18000.0f;

    // T70: separate vertical windows. The audio spectrum is dBFS (0 dBFS at
    // the top, -72 dB at the bottom); the crossover envelopes and the grid use
    // a +/-18 dB reference scale so the passband sits in the middle.
    static constexpr float spectrumMaxDb = 0.0f;
    static constexpr float spectrumMinDb = -72.0f;
    static constexpr float envelopeMaxDb = 18.0f;
    static constexpr float envelopeMinDb = -18.0f;

    /** Logarithmic frequency -> x in [0, width] (clamped at both ends). */
    static float freqToX (float freqHz, float width) noexcept;

    /** Inverse of freqToX (clamped to [minFreqHz, maxFreqHz]). */
    static float xToFreq (float x, float width) noexcept;

    /** T70: dBFS -> y in [topY, bottomY] using the spectrum window. */
    static float spectrumDbToY (float db, float topY, float bottomY) noexcept;

    /** T70: envelope dB -> y in [topY, bottomY] using the +/-18 dB window. */
    static float envelopeDbToY (float db, float topY, float bottomY) noexcept;

    /** Drag-layer constraint: xover1 stays at least 1.05x below xover2
        (mirrors CrossoverBank::minFreqRatio, SPEC §4.4 — audit P2-7). */
    static float clampXover1 (float freqHz, float xover2Hz) noexcept;

    /** Drag-layer constraint: xover2 stays at least 1.05x above xover1. */
    static float clampXover2 (float freqHz, float xover1Hz) noexcept;

    /** Audit P2-7: which controls have an audible effect for a band count.
        `control` 0 = Xover 1 (used with >=2 bands), 1 = Xover 2 (only with
        3 bands); `band` 0-based Band Gain knob. */
    static bool xoverKnobApplies (int control, int bandCount) noexcept;
    static bool bandGainApplies (int band, int bandCount) noexcept;

    /** Hann-windowed FFT magnitude spectrum (fftSize/2 positive bins).
        Pure — unit-tested; the display path uses the 2048-point variant.
        Callers own all buffers. */
    static void computeMagnitude (juce::dsp::FFT& fft, const float* timeIn,
                                  float* magnitudeOut) noexcept;

    /** T62: current well rectangle (design px) — used by layout/pixel tests. */
    juce::Rectangle<float> getGraphArea() const noexcept { return graphArea; }

    /** Test/tooling hook: run the FFT refresh synchronously (headless tests
        have no dispatch loop, so the 60 Hz timer may never fire). */
    void refreshSpectrumForTest() { refreshSpectrum(); }

    /** Test/tooling hook: re-apply the band-count-dependent enable states. */
    void updateControlEnablementForTest() { updateControlEnablement(); }

    //==========================================================================
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    //==========================================================================
    /** T39: while a Sideband Low Cut knob is being dragged, its band card
        calls this every value change — the display draws the live high-pass
        envelope (brighter than the band envelopes). Message thread only.
        T62: dormant while the Sideband UI is hidden; kept for restoration. */
    void showLowcutEnvelope (int bandOneBased, float fcHz) noexcept;
    /** Drag released: remove the preview line. */
    void hideLowcutEnvelope() noexcept;

private:
    void timerCallback() override;
    void refreshSpectrum();
    void updateControlEnablement();
    void drawBackground (juce::Graphics&, const juce::Rectangle<float>&);
    void drawGrid (juce::Graphics&, const juce::Rectangle<float>&);
    void drawSlopeEnvelope (juce::Graphics&, const juce::Rectangle<float>&);
    void drawSpectrumCurve (juce::Graphics&, const juce::Rectangle<float>&);
    void drawCrossoverLines (juce::Graphics&, const juce::Rectangle<float>&);
    void drawDragOverlay (juce::Graphics&, const juce::Rectangle<float>&);

    int numVisibleLines() const noexcept;
    int getSlopeDbPerOctave() const noexcept;
    float lineX (int index, float width) const noexcept;
    float getXoverHz (int index) const noexcept;       // effective (1.05x clamp)
    float getStoredXoverHz (int index) const noexcept; // raw parameter value
    void setXoverHz (int index, float freqHz) noexcept;
    int hitTestCrossover (float x) const noexcept;
    void endDragGesture() noexcept;
    void drawLowcutEnvelope (juce::Graphics&, const juce::Rectangle<float>&);

    //==========================================================================
    juce::AudioProcessorValueTreeState& apvts;
    ui::SpectrumSource& spectrum;
    ui::SpectrumSource& spectrumOut;

    // T39: live Low Cut preview state (0 Hz = hidden). Drawn on top so the
    // dragging feedback is never hidden under the spectrum fill.
    int   lowcutPeekBand = 0;
    float lowcutPeekHz = 0.0f;

    // Display mode for the spectrum window (T37): input / output / both.
    enum class DisplayMode { input, output, both };
    DisplayMode displayMode = DisplayMode::input;
    void setDisplayMode (DisplayMode m) noexcept;

    // FFT (UI thread, 60 Hz — no RT constraints, but everything preallocated).
    juce::dsp::FFT fft { 11 };                  // 2048 points
    static constexpr int fftSize = 2048;
    std::array<float, fftSize> fftTime {};
    std::array<float, fftSize / 2> magnitude {};
    std::array<float, fftSize / 2> magnitudeOut {};

    // One level per pixel column (resized on layout changes, UI thread).
    std::vector<float> levels;
    std::vector<float> levelsOut;
    std::vector<float> smoothed;
    std::vector<float> smoothedOut;
    juce::Rectangle<float> graphArea;

    // Controls: band count radios (1/2/3) + slope combo (right-aligned).
    std::array<juce::ToggleButton, 3> bandCountRadios;
    juce::ComboBox slopeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> slopeAttachment;

    // Knob row: 2 xover frequency knobs + 3 band-gain knobs (with labels,
    // T37 follow-up — same textbox style as the band-card knobs).
    juce::Label xover1Label;
    juce::Label xover2Label;
    std::array<juce::Label, 3> gainLabels {
        juce::Label {}, juce::Label {}, juce::Label {} };
    // Batch 2: same vertical drag feel as every other knob (Band/Master).
    juce::Slider xover1Knob { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Slider xover2Knob { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
    std::array<juce::Slider, 3> gainKnobs {
        juce::Slider { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow },
        juce::Slider { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow },
        juce::Slider { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow } };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> xover1Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> xover2Attachment;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> gainAttachments;

    // Spectrum display-mode selectors (radio, same row as the knobs).
    std::array<juce::ToggleButton, 3> displayModeRadios;
    void syncBandRadios() noexcept;
    void syncDisplayRadios() noexcept;

    // Drag state.
    int dragIndex = -1;                         // 0 = xover1, 1 = xover2
    float dragFreqHz = 0.0f;
    int hoverIndex = -1;                        // §5.12 hover emphasis

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverDisplay)
};

} // namespace ui
