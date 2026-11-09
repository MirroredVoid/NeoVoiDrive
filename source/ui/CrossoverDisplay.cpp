#include "ui/CrossoverDisplay.h"

#include "dsp/CrossoverBank.h"
#include "params/LoudnessCurve.h"
#include "ui/NeoLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace ui
{

//==============================================================================
// ---- pure helpers ----
//==============================================================================
float CrossoverDisplay::freqToX (float freqHz, float width) noexcept
{
    static const float logMin = std::log (minFreqHz);
    static const float logMax = std::log (maxFreqHz);

    const float f = juce::jlimit (minFreqHz, maxFreqHz, freqHz);
    const float t = (std::log (f) - logMin) / (logMax - logMin);
    return t * width;
}

float CrossoverDisplay::xToFreq (float x, float width) noexcept
{
    static const float logMin = std::log (minFreqHz);
    static const float logMax = std::log (maxFreqHz);

    const float t = juce::jlimit (0.0f, width, x) / juce::jmax (width, 1.0f);
    return std::exp (logMin + t * (logMax - logMin));
}

float CrossoverDisplay::spectrumDbToY (float db, float topY, float bottomY) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f,
                                  (db - spectrumMinDb) / (spectrumMaxDb - spectrumMinDb));
    return bottomY - t * (bottomY - topY);
}

float CrossoverDisplay::envelopeDbToY (float db, float topY, float bottomY) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f,
                                  (db - envelopeMinDb) / (envelopeMaxDb - envelopeMinDb));
    return bottomY - t * (bottomY - topY);
}

float CrossoverDisplay::clampXover1 (float freqHz, float xover2Hz) noexcept
{
    // SPEC §4.4 / CrossoverBank::minFreqRatio: the DSP raises f2 to at least
    // 1.05 x f1, so the UI must not let f1 get closer than that (audit P2-7).
    const auto maxF1 = xover2Hz / dsp::CrossoverBank::minFreqRatio;
    return juce::jlimit (minFreqHz, juce::jmax (minFreqHz, maxF1), freqHz);
}

float CrossoverDisplay::clampXover2 (float freqHz, float xover1Hz) noexcept
{
    const auto minF2 = xover1Hz * dsp::CrossoverBank::minFreqRatio;
    return juce::jlimit (juce::jmin (maxFreqHz, minF2), maxFreqHz, freqHz);
}

bool CrossoverDisplay::xoverKnobApplies (int control, int bandCount) noexcept
{
    return control == 0 ? bandCount >= 2 : bandCount >= 3;
}

bool CrossoverDisplay::bandGainApplies (int band, int bandCount) noexcept
{
    return band >= 0 && band < bandCount;
}

void CrossoverDisplay::computeMagnitude (juce::dsp::FFT& fft, const float* timeIn,
                                         float* magnitudeOut) noexcept
{
    // performRealOnlyForwardTransform is IN-PLACE: copy the input into a
    // scratch buffer first, then transform it in place.
    std::array<float, 2 * fftSize> freq {}; // see header: 2 * getSize() floats
    std::copy (timeIn, timeIn + fftSize, freq.begin());
    fft.performRealOnlyForwardTransform (freq.data());

    for (int i = 0; i < fftSize / 2; ++i)
    {
        const float re = freq[static_cast<size_t> (2 * i)];
        const float im = freq[static_cast<size_t> (2 * i + 1)];
        magnitudeOut[i] = std::hypot (re, im);
    }
}

//==============================================================================
CrossoverDisplay::CrossoverDisplay (juce::AudioProcessorValueTreeState& apvts,
                                    ui::SpectrumSource& spectrum,
                                    ui::SpectrumSource& spectrumOut)
    : apvts (apvts), spectrum (spectrum), spectrumOut (spectrumOut)
{
    dopamine::applySection (*this, dopamine::Section::spectrum);

    // Band-count radios (T37): 3-choice radio group replacing the combo.
    for (int i = 0; i < 3; ++i)
    {
        bandCountRadios[static_cast<size_t> (i)].setRadioGroupId (0x3B1);
        bandCountRadios[static_cast<size_t> (i)].setClickingTogglesState (true);
        bandCountRadios[static_cast<size_t> (i)].setComponentID ("band_radio_" + juce::String (i));
        bandCountRadios[static_cast<size_t> (i)].setButtonText (juce::String (i + 1) + " band"
                                                                + (i == 0 ? "" : "s"));
        bandCountRadios[static_cast<size_t> (i)].onClick = [this, i]
        {
            if (bandCountRadios[static_cast<size_t> (i)].getToggleState())
                if (auto* param = this->apvts.getParameter (params::IDs::bandCount))
                    param->setValueNotifyingHost (static_cast<float> (i) / 2.0f);
        };
        addAndMakeVisible (bandCountRadios[static_cast<size_t> (i)]);
    }
    syncBandRadios();

    slopeCombo.addItemList ({ "12 dB/oct", "24 dB/oct", "36 dB/oct",
                              "48 dB/oct", "60 dB/oct", "72 dB/oct" }, 1);
    // Batch 2: crossover slope is frequency semantics; the combo's role cue
    // (accent chevron) now matches the teal Xover knobs and cursor.
    dopamine::applyRole (slopeCombo, dopamine::Role::frequency);
    addAndMakeVisible (slopeCombo);
    slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, params::IDs::xoverSlope, slopeCombo);

    // Knob row: 2 xover frequency knobs (reuse the existing parameters).
    // T37 follow-up: same look as the band-card knobs — label above, compact
    // transparent textbox below (no border), unit suffix, <=3-digit format.
    const auto styleKnob = [] (juce::Slider& slider, const juce::String& paramID)
    {
        slider.setColour (juce::Slider::textBoxTextColourId, NeoLookAndFeel::Tokens::ink());
        slider.setColour (juce::Slider::textBoxBackgroundColourId, dopamine::transparent());
        slider.setColour (juce::Slider::textBoxOutlineColourId, dopamine::transparent());
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18); // §7 size follow-up
        slider.setTextValueSuffix (NeoLookAndFeel::unitForParam (paramID));
    };
    const auto styleLabel = [] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (13.0f)); // §7 parameter name
        label.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::inkSoft());
    };

    styleKnob (xover1Knob, params::IDs::xover1Freq);
    styleKnob (xover2Knob, params::IDs::xover2Freq);
    xover1Knob.setComponentID ("xover1Knob");
    xover2Knob.setComponentID ("xover2Knob");
    dopamine::applyRole (xover1Knob, dopamine::Role::frequency);
    dopamine::applyRole (xover2Knob, dopamine::Role::frequency);
    // Frequencies display as whole Hz (T37 follow-up); the band gains keep
    // the compact <=3-digit format.
    xover1Knob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
    xover2Knob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
    styleLabel (xover1Label, "Xover 1");
    styleLabel (xover2Label, "Xover 2");
    xover1Knob.setTooltip ("Band 1 / band 2 crossover");
    xover2Knob.setTooltip ("Band 2 / band 3 crossover");
    addAndMakeVisible (xover1Knob);
    addAndMakeVisible (xover2Knob);
    addAndMakeVisible (xover1Label);
    addAndMakeVisible (xover2Label);
    xover1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, params::IDs::xover1Freq, xover1Knob);
    xover2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, params::IDs::xover2Freq, xover2Knob);
    // SliderAttachment overwrites textFromValueFunction — re-apply (band-card
    // convention) and refresh the textbox.
    xover1Knob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
    xover2Knob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
    xover1Knob.updateText();
    xover2Knob.updateText();

    // 3 band-gain knobs (SPEC §4.3: -48..+12 dB, default 0).
    static constexpr const char* gainIds[3] = { params::IDs::b1_gain,
                                                params::IDs::b2_gain,
                                                params::IDs::b3_gain };
    static constexpr const char* gainTexts[3] = { "Band 1 Gain", "Band 2 Gain", "Band 3 Gain" };
    for (int i = 0; i < 3; ++i)
    {
        styleKnob (gainKnobs[static_cast<size_t> (i)], gainIds[i]);
        gainKnobs[static_cast<size_t> (i)].setComponentID ("bandGainKnob" + juce::String (i + 1));
        dopamine::applyRole (gainKnobs[static_cast<size_t> (i)], dopamine::Role::level);
        // Batch 2 two-axis encoding: the arc stays Level orange ("what"),
        // while the knob face/groove and its label carry the band accent
        // ("which band") so the gain knobs tie back to the plot colours.
        dopamine::applySection (gainKnobs[static_cast<size_t> (i)],
                                dopamine::sectionForBand (i + 1));
        styleLabel (gainLabels[static_cast<size_t> (i)], gainTexts[i]);
        gainLabels[static_cast<size_t> (i)].setColour (
            juce::Label::textColourId,
            dopamine::mix (dopamine::bandAccent (i),
                           dopamine::surface (dopamine::Surface::ink), 0.55f));
        gainKnobs[static_cast<size_t> (i)].setTooltip (juce::String ("Band ") + juce::String (i + 1) + " gain");
        addAndMakeVisible (gainKnobs[static_cast<size_t> (i)]);
        addAndMakeVisible (gainLabels[static_cast<size_t> (i)]);
        gainAttachments[static_cast<size_t> (i)] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts, gainIds[i], gainKnobs[static_cast<size_t> (i)]);
        gainKnobs[static_cast<size_t> (i)].textFromValueFunction =
            [] (double v)
            {
                // T71: the stored -48 dB sentinel is the loudness knob's mute
                // position; show -inf while the parameter keeps a finite dB.
                if (params::loudness::isMuteDb (static_cast<float> (v)))
                    return params::loudness::muteText();
                if (std::abs (v) < 1.0e-3)
                    v = 0.0; // avoid "-0 dB" from a tiny negative round-off
                return NeoLookAndFeel::formatKnobValue (v);
            };
        gainKnobs[static_cast<size_t> (i)].updateText();
    }

    // Spectrum display-mode radios: input / output / both.
    static constexpr const char* modeTexts[3] = { "In", "Out", "Both" };
    for (int i = 0; i < 3; ++i)
    {
        displayModeRadios[static_cast<size_t> (i)].setRadioGroupId (0x3B2);
        displayModeRadios[static_cast<size_t> (i)].setClickingTogglesState (true);
        displayModeRadios[static_cast<size_t> (i)].setButtonText (modeTexts[i]);
        displayModeRadios[static_cast<size_t> (i)].onClick = [this, i]
        {
            if (displayModeRadios[static_cast<size_t> (i)].getToggleState())
                setDisplayMode (static_cast<DisplayMode> (i));
        };
        addAndMakeVisible (displayModeRadios[static_cast<size_t> (i)]);
    }
    syncDisplayRadios();
    updateControlEnablement();

    startTimerHz (60);
}

CrossoverDisplay::~CrossoverDisplay()
{
    stopTimer();

    // A window closed mid-drag must not leave the host automation gesture
    // open (audit P3).
    if (dragIndex >= 0)
    {
        if (auto* param = apvts.getParameter (dragIndex == 0 ? params::IDs::xover1Freq
                                                             : params::IDs::xover2Freq))
            param->endChangeGesture();
        dragIndex = -1;
    }
}

//==============================================================================
void CrossoverDisplay::resized()
{
    // UI_STYLE_GUIDE follow-up: inner padding so the controls and the well
    // no longer touch the card edges (user report: radio/combo glued to the
    // sides, well spanned 100 % width).
    auto area = getLocalBounds().reduced (12, 8);

    // Top control row: band radios on the left, slope combo right-aligned.
    // §7: 14 px radio text needs an 82 px cell ("3 bands" ≈ 59 px + dot 16).
    auto controls = area.removeFromTop (28);
    auto radioZone = controls.removeFromLeft (250);
    for (int i = 0; i < 3; ++i)
        bandCountRadios[static_cast<size_t> (i)].setBounds (radioZone.removeFromLeft (82).reduced (0, 3));
    slopeCombo.setBounds (controls.removeFromRight (120).reduced (0, 3));

    // Bottom knob row: 2 frequency knobs | gap | 3 gain knobs | gap |
    // display-mode radios (right-aligned, T37 follow-up).
    // T63: the left column is 754 px wide (730 px inner) — wider 90 px cells
    // spread the five knobs across the well; the display-mode radios stay
    // right-aligned.
    auto knobs = area.removeFromBottom (76);
    auto freqZone = knobs.removeFromLeft (180);
    const auto placeKnob = [] (juce::Rectangle<int>& zone, juce::Label& label, juce::Slider& slider)
    {
        auto cell = zone.removeFromLeft (90);
        label.setBounds (cell.removeFromTop (14));
        slider.setBounds (cell);
    };
    placeKnob (freqZone, xover1Label, xover1Knob);
    placeKnob (freqZone, xover2Label, xover2Knob);
    knobs.removeFromLeft (16);   // group gap
    auto gainZone = knobs.removeFromLeft (270);
    for (int i = 0; i < 3; ++i)
        placeKnob (gainZone, gainLabels[static_cast<size_t> (i)], gainKnobs[static_cast<size_t> (i)]);
    knobs.removeFromLeft (12);
    auto modeZone = knobs;
    // Right-aligned (T37 follow-up): place from the right edge backwards.
    // §7/T63: 72 px cells fit the 14 px radio text ("Both" ≈ 34 px + dot 16).
    for (int i = 2; i >= 0; --i)
        displayModeRadios[static_cast<size_t> (i)].setBounds (modeZone.removeFromRight (72).reduced (0, 27));

    graphArea = area.toFloat().withTrimmedTop (4.0f).withTrimmedBottom (4.0f);

    const auto w = static_cast<size_t> (std::max (0, getWidth()));
    levels.assign (w, 0.0f);
    levelsOut.assign (w, 0.0f);
    smoothed.assign (w, 0.0f);
    smoothedOut.assign (w, 0.0f);
}

//==============================================================================
void CrossoverDisplay::setDisplayMode (DisplayMode m) noexcept
{
    displayMode = m;
    syncDisplayRadios();
    refreshSpectrum();
    repaint();
}

// Hann window in place (30 Hz refresh — negligible cost).
static void applyHann (std::array<float, 2048>& timeIn) noexcept
{
    constexpr double twoPi = 6.283185307179586;
    const auto size = static_cast<int> (timeIn.size());
    for (int i = 0; i < size; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (twoPi * i / (size - 1));
        timeIn[static_cast<size_t> (i)] = static_cast<float> (timeIn[static_cast<size_t> (i)] * w);
    }
}

void CrossoverDisplay::syncBandRadios() noexcept
{
    // getRawParameterValue returns the RAW (denormalised) value — for a
    // choice parameter that is the INDEX (0/1/2), not the normalised 0..1.
    // (The old load()*2 mapping showed 2 bands as 3 bands — T37 bug.)
    const int index = juce::jlimit (0, 2, static_cast<int> (std::lround (
        apvts.getRawParameterValue (params::IDs::bandCount)->load())));
    for (int i = 0; i < 3; ++i)
        bandCountRadios[static_cast<size_t> (i)].setToggleState (i == index, juce::dontSendNotification);
}

void CrossoverDisplay::syncDisplayRadios() noexcept
{
    for (int i = 0; i < 3; ++i)
        displayModeRadios[static_cast<size_t> (i)].setToggleState (
            i == static_cast<int> (displayMode), juce::dontSendNotification);
}

//==============================================================================
void CrossoverDisplay::updateControlEnablement()
{
    // Audit P2-7: controls that the current band count cannot use are
    // disabled and greyed instead of silently doing nothing.
    const int bands = numVisibleLines() + 1; // 0 lines -> 1 band

    const auto apply = [] (juce::Slider& knob, juce::Label& label, bool enabled,
                           juce::Colour activeColour)
    {
        knob.setEnabled (enabled);
        label.setEnabled (enabled);
        label.setColour (juce::Label::textColourId,
                         enabled ? activeColour : NeoLookAndFeel::Tokens::disabledText());
    };

    const auto labelColour = NeoLookAndFeel::Tokens::inkSoft();
    apply (xover1Knob, xover1Label, xoverKnobApplies (0, bands), labelColour);
    apply (xover2Knob, xover2Label, xoverKnobApplies (1, bands), labelColour);

    for (int i = 0; i < 3; ++i)
    {
        const auto bandColour = dopamine::mix (dopamine::bandAccent (i),
                                              dopamine::surface (dopamine::Surface::ink),
                                              0.55f);
        apply (gainKnobs[static_cast<size_t> (i)],
               gainLabels[static_cast<size_t> (i)],
               bandGainApplies (i, bands),
               bandColour);
    }

    xover2Knob.setTooltip (bands >= 3 ? "Band 2 / band 3 crossover"
                                      : "Only used with 3 bands");
}

//==============================================================================
void CrossoverDisplay::timerCallback()
{
    syncBandRadios();
    updateControlEnablement();
    refreshSpectrum();
    repaint();
}

void CrossoverDisplay::refreshSpectrum()
{
    if (graphArea.getWidth() <= 1.0f)
        return;

    // Audit P2-1: only the displayed source is consumed, and it always keeps
    // just the newest FFT block (readNewestBlock). The hidden source is
    // drained every tick, so selecting Out/Both later shows fresh data
    // instead of a FIFO backlog (the write side drops on full).
    if (displayMode != DisplayMode::output)
    {
        if (spectrum.readNewestBlock (fftTime.data(), fftSize) == fftSize)
        {
            applyHann (fftTime);
            computeMagnitude (fft, fftTime.data(), magnitude.data());
        }
    }
    else
    {
        spectrum.discardAll();
    }

    if (displayMode != DisplayMode::input)
    {
        if (spectrumOut.readNewestBlock (fftTime.data(), fftSize) == fftSize)
        {
            applyHann (fftTime);
            computeMagnitude (fft, fftTime.data(), magnitudeOut.data());
        }
    }
    else
    {
        spectrumOut.discardAll();
    }

    // Normalise to 0 dBFS: a full-scale sine at bin k peaks at A*N/2 in the
    // raw FFT, and the Hann window halves that (coherent gain 0.5), so
    // A = mag * 4 / N.
    constexpr float norm = 4.0f / static_cast<float> (fftSize);
    const auto width = static_cast<int> (graphArea.getWidth());
    const float binHz = static_cast<float> (spectrum.getEffectiveSampleRate()) / static_cast<float> (fftSize);

    // Per-column max -> forward-fill empty low columns (contiguous curve).
    const auto fillLevels = [&] (const std::array<float, fftSize / 2>& mag,
                                 std::vector<float>& lv, std::vector<float>& sm)
    {
        std::fill (lv.begin(), lv.end(), 0.0f);
        for (int b = 1; b < fftSize / 2; ++b)
        {
            const float f = static_cast<float> (b) * binHz;
            const int col = juce::jlimit (0, width - 1,
                                          static_cast<int> (freqToX (f, static_cast<float> (width))));
            lv[static_cast<size_t> (col)] = juce::jmax (lv[static_cast<size_t> (col)],
                                                        mag[static_cast<size_t> (b)] * norm);
        }
        float last = 0.0f;
        for (size_t i = 0; i < lv.size(); ++i)
        {
            if (lv[i] > 0.0f)
                last = lv[i];
            else
                lv[i] = last;
            // T39: smoother curve — attack keeps transients, slower release
            // removes the frame-to-frame flicker at 60 fps.
            const float a = lv[i] > sm[i] ? 0.55f : 0.18f;
            sm[i] = sm[i] + a * (lv[i] - sm[i]);
        }
    };

    if (displayMode != DisplayMode::output)
        fillLevels (magnitude, levels, smoothed);
    if (displayMode != DisplayMode::input)
        fillLevels (magnitudeOut, levelsOut, smoothedOut);
}

//==============================================================================
void CrossoverDisplay::paint (juce::Graphics& g)
{
    // T37: re-sync the band radios from the parameter on every paint — the
    // 30 Hz timer may be starved in headless tests, and this guarantees the
    // selection always reflects the actual parameter (user report: 2 bands
    // showed as 3 bands).
    syncBandRadios();

    // UI_STYLE_GUIDE T64: flat neumorphic card + light inset well.
    const auto section = dopamine::Section::spectrum;
    NeoLookAndFeel::drawNeoPanel (g, getLocalBounds().toFloat(),
                                  NeoLookAndFeel::Tokens::radiusMedium,
                                  section, false);

    if (graphArea.getWidth() <= 1.0f)
        return;

    const auto wellR = NeoLookAndFeel::Tokens::radiusWell;
    g.setColour (dopamine::sectionWell (section));
    g.fillRoundedRectangle (graphArea, wellR);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path wellClip;
        wellClip.addRoundedRectangle (graphArea.reduced (0.5f), wellR);
        g.reduceClipRegion (wellClip);

        drawBackground (g, graphArea);
        drawGrid (g, graphArea);
        drawSlopeEnvelope (g, graphArea);
        drawSpectrumCurve (g, graphArea);
        drawCrossoverLines (g, graphArea);
        drawLowcutEnvelope (g, graphArea);   // T39: on top of the spectrum
    }

    // Drag handle + frequency tag float ABOVE the well (not clipped).
    drawDragOverlay (g, graphArea);

    NeoLookAndFeel::drawNeoInset (g, graphArea, wellR, section, true);
}

void CrossoverDisplay::drawBackground (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    const auto width = area.getWidth();
    const int n = numVisibleLines();
    const auto x1 = n >= 1 ? lineX (0, width) : -1.0f;
    const auto x2 = n >= 2 ? lineX (1, width) : -1.0f;

    // Band regions: each band's identity accent as a very light tint.
    const auto tint = [&] (int band, float alpha)
    {
        return dopamine::bandAccent (band).withAlpha (alpha);
    };

    if (n == 0)
    {
        g.setColour (tint (0, 0.055f));
        g.fillRect (area);
    }
    else if (n == 1)
    {
        g.setColour (tint (0, 0.060f));
        g.fillRect (area.withRight (x1));
        g.setColour (tint (1, 0.032f));
        g.fillRect (area.withLeft (x1));
    }
    else
    {
        g.setColour (tint (0, 0.060f));
        g.fillRect (area.withRight (x1));
        g.setColour (tint (1, 0.034f));
        g.fillRect (juce::Rectangle<float> (x1, area.getY(), x2 - x1, area.getHeight()));
        g.setColour (tint (2, 0.055f));
        g.fillRect (area.withLeft (x2));
    }
}

void CrossoverDisplay::drawGrid (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    // T70: thin, low-opacity grid; inkFaint at a low alpha stays visible on
    // the near-white well (the surface line colour is almost the same value).
    const auto gridColour = dopamine::surface (dopamine::Surface::inkFaint);
    g.setColour (gridColour.withAlpha (0.12f));

    static constexpr float gridFreqs[] = { 100.0f, 1000.0f, 10000.0f };
    for (float f : gridFreqs)
    {
        const float x = area.getX() + freqToX (f, area.getWidth());
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }

    g.setColour (NeoLookAndFeel::Tokens::inkSoft());
    g.setFont (juce::FontOptions (12.0f));
    for (float f : gridFreqs)
    {
        const float x = area.getX() + freqToX (f, area.getWidth());
        const juce::String label = f >= 1000.0f
                                 ? juce::String (static_cast<int> (f / 1000.0f)) + "k"
                                 : juce::String (static_cast<int> (f));
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, area.getBottom() - 14.0f, 40.0f, 12.0f),
                    juce::Justification::centred, false);
    }

    // Edge labels.
    g.drawText ("40", juce::Rectangle<float> (area.getX(), area.getBottom() - 14.0f, 40.0f, 12.0f),
                juce::Justification::centred, false);
    g.drawText ("18k", juce::Rectangle<float> (area.getRight() - 40.0f, area.getBottom() - 14.0f, 40.0f, 12.0f),
                juce::Justification::centred, false);

    // T70: the horizontal grid follows the envelope window (+18..-18 dB,
    // 6 dB steps). No vertical-axis labels — the scale is only a visual
    // reference for the crossover envelopes.
    const auto bottom = area.getBottom() - 2.0f;
    const auto top = area.getY() + 16.0f;

    for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
    {
        g.setColour (gridColour.withAlpha (db == 0.0f ? 0.24f : 0.14f));
        g.drawHorizontalLine (juce::roundToInt (envelopeDbToY (db, top, bottom)),
                              area.getX(), area.getRight());
    }
}

void CrossoverDisplay::drawSlopeEnvelope (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    // Ideal per-band response (Linkwitz-Riley): drawn with the SAME real-
    // transfer-function model as the Low Cut preview — an LR_N is two N/2-
    // order Butterworth stages cascaded, so with half-order k = slope/12:
    //   power ratio (single stage, HP) = r^(2k) / (1 + r^(2k))
    //   envelope dB                    = 20·log10(power ratio)   [= 2 stages]
    // which passes exactly through −6.02 dB at each crossover and falls at
    // −slope dB/oct into the stop band (the old straight-line ramp overshot
    // the passband above 0 dB, same artifact the preview had).
    const int n = numVisibleLines();
    if (n == 0)
        return;

    const int slope = getSlopeDbPerOctave();
    const int halfOrder = juce::jmax (1, slope / 12);
    const auto width = area.getWidth();
    const float fc1 = getXoverHz (0);
    const float fc2 = n >= 2 ? getXoverHz (1) : 0.0f;

    // T70: envelope window is [-18, +18] dB (top = +18 dB, bottom = -18 dB),
    // independent of the spectrum's dBFS scale.
    constexpr float dbFloor = envelopeMinDb;
    const auto bottom = area.getBottom() - 2.0f;
    const auto top = area.getY() + 16.0f;

    // Per-band gain (SPEC §4.3): the envelope follows the gain knobs.
    const auto bandGainDb = [this] (int band)
    {
        const auto* param = apvts.getParameter (band == 0 ? params::IDs::b1_gain
                                                : band == 1 ? params::IDs::b2_gain
                                                            : params::IDs::b3_gain);
        if (param == nullptr)
            return 0.0f;
        return param->getNormalisableRange().convertFrom0to1 (param->getValue());
    };

    const auto yForDb = [&] (float db)
    {
        return envelopeDbToY (db, top, bottom);
    };

    // Single-stage power ratios (both sides of one crossover, half-order k).
    const auto pwrLow = [halfOrder] (float f, float fc) -> double
    {
        if (fc <= 0.0f || f <= 0.0f)
            return 1.0;
        const double r = static_cast<double> (f) / static_cast<double> (fc);
        return 1.0 / (1.0 + std::pow (r, 2.0 * halfOrder));
    };
    const auto pwrHigh = [halfOrder] (float f, float fc) -> double
    {
        if (fc <= 0.0f || f <= 0.0f)
            return 1.0;
        const double r = static_cast<double> (f) / static_cast<double> (fc);
        const double p = std::pow (r, 2.0 * halfOrder);
        return p / (1.0 + p);
    };
    const auto attLow  = [&] (float f, float fc) -> float
    {
        return static_cast<float> (20.0 * std::log10 (pwrLow (f, fc)));   // 2 stages
    };
    const auto attHigh = [&] (float f, float fc) -> float
    {
        return static_cast<float> (20.0 * std::log10 (pwrHigh (f, fc)));
    };

    // T64: each band curve uses its identity accent (pink / amber / yellow).
    const juce::Colour envColours[] =
    {
        dopamine::bandAccent (0),
        dopamine::bandAccent (1),
        dopamine::bandAccent (2),
    };

    for (int band = 0; band <= n; ++band)
    {
        juce::Path path;
        bool started = false;
        for (int px = 0; px <= static_cast<int> (width); ++px)
        {
            const float f = xToFreq (static_cast<float> (px), width);
            float db = 0.0f;
            if (n == 1)
                db = band == 0 ? attLow (f, fc1) : attHigh (f, fc1);
            else if (band == 0)      db = attLow (f, fc1);
            else if (band == 1)      db = attHigh (f, fc1) + attLow (f, fc2); // band-pass sum
            else                     db = attHigh (f, fc2);

            db += bandGainDb (band);   // T37: envelope follows the band gain

            const float y = yForDb (juce::jmax (dbFloor, db));
            if (! started)
            {
                path.startNewSubPath (area.getX() + static_cast<float> (px), y);
                started = true;
            }
            else
            {
                path.lineTo (area.getX() + static_cast<float> (px), y);
            }
        }

        const auto accent = envColours[band % 3];

        // All three envelopes share one silhouette: the same soft dark
        // under-stroke + identity-colour core. Band 3's bright yellow needs
        // the edge for contrast, and giving every band the identical edge
        // keeps the three strokes visually the same weight (user report:
        // the yellow curve looked thicker than pink/amber).
        g.setColour (dopamine::mix (accent, dopamine::surface (dopamine::Surface::ink), 0.55f)
                         .withAlpha (0.30f));
        g.strokePath (path, juce::PathStrokeType (3.4f));

        g.setColour (accent.withAlpha (0.95f));
        g.strokePath (path, juce::PathStrokeType (2.4f));
    }
}

//==============================================================================
// T39/T62: live Sideband Low Cut preview. The band card forwarded the knob's
// drag lifecycle; T62 hides the Sideband UI, so this renderer is dormant but
// kept for restoration. The envelope vanishes on release when active.
//==============================================================================
void CrossoverDisplay::showLowcutEnvelope (int bandOneBased, float fcHz) noexcept
{
    lowcutPeekBand = juce::jlimit (1, 3, bandOneBased);
    lowcutPeekHz = juce::jlimit (100.0f, 8000.0f, fcHz);
    repaint();
}

void CrossoverDisplay::hideLowcutEnvelope() noexcept
{
    lowcutPeekHz = 0.0f;
    repaint();
}

void CrossoverDisplay::drawLowcutEnvelope (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    if (lowcutPeekHz <= 0.0f || lowcutPeekBand == 0)
        return;

    // T70: envelope window is [-18, +18] dB (top = +18 dB, bottom = -18 dB),
    // independent of the spectrum's dBFS scale.
    constexpr float dbFloor = envelopeMinDb;
    const auto bottom = area.getBottom() - 2.0f;
    const auto top = area.getY() + 16.0f;
    const auto width = area.getWidth();

    // SidebandEngine HP: 24 dB/oct Butterworth high-pass — the REAL
    // transfer function of two cascaded biquads, |H|² = (ω⁴/(ω⁴+ωc⁴))²,
    // so the curve passes exactly through −6.02 dB at fc (two −3.01 dB
    // stages) with a −24 dB/oct skirt.
    const auto attHp = [] (float f, float fc) -> float
    {
        if (fc <= 0.0f || f <= 0.0f)
            return 0.0f;
        const double r = static_cast<double> (f) / static_cast<double> (fc);
        const double r2 = r * r;
        const double stage = (r2 * r2) / (r2 * r2 + 1.0);
        return static_cast<float> (20.0 * std::log10 (stage));
    };

    juce::Path path;
    bool started = false;
    for (int px = 0; px <= static_cast<int> (width); ++px)
    {
        const float f = xToFreq (static_cast<float> (px), width);
        const float y = envelopeDbToY (juce::jmax (dbFloor, attHp (f, lowcutPeekHz)),
                                       top, bottom);
        if (! started) { path.startNewSubPath (area.getX() + static_cast<float> (px), y); started = true; }
        else           { path.lineTo (area.getX() + static_cast<float> (px), y); }
    }

    // Brighter than the band envelopes: the user asked for a DIFFERENT hue
    // family + a thinner stroke — the band's identity accent, dashed, with a
    // soft glow, clearly not a band boundary.
    const auto peekColour = dopamine::bandAccent (lowcutPeekBand - 1);
    g.setColour (peekColour.withAlpha (0.20f));
    g.strokePath (path, juce::PathStrokeType (3.5f));
    g.setColour (peekColour.withAlpha (0.90f));
    const float dashPattern[] = { 8.0f, 6.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.4f).createDashedStroke (dashed, path,
                                                    dashPattern, 2);
    g.strokePath (dashed, juce::PathStrokeType (1.4f));
}

void CrossoverDisplay::drawSpectrumCurve (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    if (smoothed.empty())
        return;

    // T70: spectrum window is 0 dBFS (top) .. -72 dBFS (bottom); the top
    // 16 px are reserved for the crossover labels.
    const auto bottom = area.getBottom() - 2.0f;
    const auto top = area.getY() + 16.0f;

    const auto drawCurve = [&] (const std::vector<float>& sm, float alpha)
    {
        // T39: 3-point moving average along x — removes the per-column
        // jaggedness left by the bin→column max mapping.
        std::vector<float> smoothX (sm.size());
        for (size_t x = 0; x < sm.size(); ++x)
        {
            const float prev = sm[x > 0 ? x - 1 : x];
            const float next = sm[x + 1 < sm.size() ? x + 1 : x];
            smoothX[x] = (prev + 2.0f * sm[x] + next) * 0.25f;
        }

        juce::Path path;
        path.startNewSubPath (area.getX(), bottom);
        for (size_t x = 0; x < smoothX.size(); ++x)
        {
            const float db = 20.0f * std::log10 (smoothX[x] + 1.0e-6f);
            path.lineTo (area.getX() + static_cast<float> (x),
                         spectrumDbToY (db, top, bottom));
        }
        path.lineTo (area.getRight(), bottom);
        path.closeSubPath();

        g.setColour (dopamine::accent (dopamine::Role::global).base.withAlpha (alpha * 0.16f));
        g.fillPath (path);
        g.setColour (dopamine::accent (dopamine::Role::global).base.withAlpha (alpha * 0.90f));
        g.strokePath (path, juce::PathStrokeType (1.5f));
    };

    switch (displayMode)
    {
        case DisplayMode::input:  drawCurve (smoothed, 1.0f); break;
        case DisplayMode::output: drawCurve (smoothedOut, 1.0f); break;
        case DisplayMode::both:
            // Input in the background at half opacity, output on top — the
            // user asked for the opacity split to keep both readable.
            drawCurve (smoothed, 0.45f);
            drawCurve (smoothedOut, 1.0f);
            break;
    }
}

void CrossoverDisplay::drawCrossoverLines (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    const int n = numVisibleLines();
    const auto width = area.getWidth();

    for (int i = 0; i < n; ++i)
    {
        const float x = lineX (i, width);
        const bool active = (dragIndex == i) || (hoverIndex == i && dragIndex < 0);

        // T64: frequency-role teal cursor; hover widens it and adds a glow.
        if (active)
        {
            g.setColour (dopamine::accent (dopamine::Role::frequency).base.withAlpha (0.18f));
            g.fillRect (x - 4.0f, area.getY(), 8.0f, area.getHeight());
        }
        g.setColour (dopamine::accent (dopamine::Role::frequency).base);
        g.drawLine (x, area.getY(), x, area.getBottom(), active ? 2.5f : 1.5f);

        // T37 fix: while dragging, the static label is suppressed — the drag
        // overlay already shows the live frequency (the old code drew both,
        // producing two overlapping frequency readouts).
        if (dragIndex == i)
            continue;

        const auto hz = getXoverHz (i);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.setColour (NeoLookAndFeel::Tokens::ink());
        g.drawText (juce::String (hz, 0) + " Hz",
                    juce::Rectangle<float> (x - 40.0f, area.getY(), 80.0f, 16.0f),
                    juce::Justification::centred, false);
    }
}

void CrossoverDisplay::drawDragOverlay (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    if (dragIndex < 0)
        return;

    const float x = lineX (dragIndex, area.getWidth());

    // §5.12: convex grab handle with a frequency-accent core ("grabbable").
    const juce::Rectangle<float> handle (x - 6.0f, area.getY() + 2.0f, 12.0f, 12.0f);
    NeoLookAndFeel::drawContactShadow (g, handle, 6.0f, 0.25f, 1.0f);
    NeoLookAndFeel::drawNeoConvexEllipse (g, handle, dopamine::Section::spectrum,
                                          NeoLookAndFeel::FaceState::normal);
    g.setColour (dopamine::accent (dopamine::Role::frequency).base);
    g.fillEllipse (x - 2.5f, area.getY() + 5.5f, 5.0f, 5.0f);

    // Live frequency tag while dragging (raised card, accent text).
    const juce::Rectangle<float> tag (x - 50.0f, area.getY() + 17.0f, 100.0f, 20.0f);
    NeoLookAndFeel::drawContactShadow (g, tag, 8.0f, 0.18f, 1.5f);
    NeoLookAndFeel::drawNeoRaised (g, tag, 8.0f, dopamine::Section::spectrum,
                                   NeoLookAndFeel::FaceState::normal, false);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (dopamine::accent (dopamine::Role::frequency).active);
    g.drawText (juce::String (dragFreqHz, 0) + " Hz", tag, juce::Justification::centred, false);
}

//==============================================================================
int CrossoverDisplay::numVisibleLines() const noexcept
{
    // band_count is a 3-choice parameter: 0 -> 1 band (no lines), 1 -> 1 line,
    // 2 -> 2 lines.
    if (auto* param = apvts.getParameter (params::IDs::bandCount))
    {
        const float v = param->getValue();
        return v < 0.34f ? 0 : (v < 0.67f ? 1 : 2);
    }
    return 2;
}

int CrossoverDisplay::getSlopeDbPerOctave() const noexcept
{
    // xover_slope is a 6-choice parameter: 0..5 -> 12/24/36/48/60/72 dB/oct.
    if (auto* param = apvts.getParameter (params::IDs::xoverSlope))
    {
        static constexpr int slopes[6] = { 12, 24, 36, 48, 60, 72 };
        return slopes[juce::jlimit (0, 5, static_cast<int> (std::lround (
            param->getValue() * 5.0f)))];
    }
    return 24;
}

float CrossoverDisplay::lineX (int index, float width) const noexcept
{
    return graphArea.getX() + freqToX (getXoverHz (index), width);
}

float CrossoverDisplay::getXoverHz (int index) const noexcept
{
    // Audit P2-7: the plot shows the EFFECTIVE crossover, matching the DSP's
    // 1.05x clamp (CrossoverBank::setCrossover raises f2 to f1 * 1.05).
    const auto stored = getStoredXoverHz (index);
    if (index == 1)
        return juce::jmin (maxFreqHz,
                           juce::jmax (stored, getStoredXoverHz (0)
                                                   * dsp::CrossoverBank::minFreqRatio));
    return stored;
}

float CrossoverDisplay::getStoredXoverHz (int index) const noexcept
{
    const auto id = index == 0 ? params::IDs::xover1Freq : params::IDs::xover2Freq;
    if (auto* param = apvts.getParameter (id))
        return param->getNormalisableRange().convertFrom0to1 (param->getValue());
    return index == 0 ? 200.0f : 2000.0f;
}

void CrossoverDisplay::setXoverHz (int index, float freqHz) noexcept
{
    const auto id = index == 0 ? params::IDs::xover1Freq : params::IDs::xover2Freq;
    if (auto* param = apvts.getParameter (id))
        param->setValueNotifyingHost (param->getNormalisableRange().convertTo0to1 (freqHz));
}

int CrossoverDisplay::hitTestCrossover (float x) const noexcept
{
    const auto width = graphArea.getWidth();
    const int n = numVisibleLines();
    for (int i = 0; i < n; ++i)
        if (std::abs (lineX (i, width) - x) < 7.0f)
            return i;
    return -1;
}

//==============================================================================
void CrossoverDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (graphArea.contains (e.position))
    {
        dragIndex = hitTestCrossover (e.position.x);
        if (dragIndex >= 0)
        {
            // M3: mark the automation gesture so DAW recording sees one
            // clean drag instead of scattered point edits.
            if (auto* param = apvts.getParameter (dragIndex == 0 ? params::IDs::xover1Freq
                                                                 : params::IDs::xover2Freq))
                param->beginChangeGesture();

            dragFreqHz = getStoredXoverHz (dragIndex);
            repaint();
        }
    }
}

void CrossoverDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0)
        return;

    const auto width = graphArea.getWidth();
    float f = xToFreq (e.position.x - graphArea.getX(), width);
    if (dragIndex == 0)
    {
        // O3: only clamp against xover2 when it is actually visible
        // (bandCount == 3); in 2-band mode the line is free up to 18k.
        // Audit P2-7: clamp against the STORED value with the DSP's 1.05x
        // ratio (the plotted xover2 may already be the effective value).
        f = numVisibleLines() > 1 ? clampXover1 (f, getStoredXoverHz (1))
                                  : clampXover1 (f, maxFreqHz);
    }
    else
    {
        f = clampXover2 (f, getStoredXoverHz (0));
    }

    setXoverHz (dragIndex, f);
    dragFreqHz = f;
    repaint();
}

void CrossoverDisplay::mouseUp (const juce::MouseEvent&)
{
    // M3: also covers capture loss — JUCE 9 delivers a synthetic mouseUp
    // when the capture is taken away mid-drag, so the gesture always ends.
    endDragGesture();
}

void CrossoverDisplay::mouseMove (const juce::MouseEvent& e)
{
    // §5.12: hover emphasis on the crossover lines (purely visual).
    const auto hit = graphArea.contains (e.position) ? hitTestCrossover (e.position.x) : -1;
    if (hit != hoverIndex)
    {
        hoverIndex = hit;
        repaint();
    }
}

void CrossoverDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex >= 0)
    {
        hoverIndex = -1;
        repaint();
    }
}

void CrossoverDisplay::endDragGesture() noexcept
{
    if (dragIndex >= 0)
    {
        if (auto* param = apvts.getParameter (dragIndex == 0 ? params::IDs::xover1Freq
                                                             : params::IDs::xover2Freq))
            param->endChangeGesture(); // M3: pair with beginChangeGesture
        dragIndex = -1;
        repaint();
    }
}

} // namespace ui
