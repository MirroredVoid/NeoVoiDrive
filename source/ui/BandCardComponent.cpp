#include "ui/BandCardComponent.h"

#include "ui/NeoLookAndFeel.h"

namespace ui
{

BandCardComponent::BandCardComponent (juce::AudioProcessorValueTreeState& apvts,
                                      int index)
    : apvts (apvts), bandIndex (index)
{
    const auto pid = [this] (juce::StringRef base)
    {
        return params::makeBandParameterID (bandIndex, base);
    };

    // T63: the Band On switch moved to the tab strip. Listen to the enable
    // parameter so DAW automation (and the tab-strip switch) repaint the
    // grey-out state without relying on an unrelated repaint.
    apvts.addParameterListener (pid (params::IDs::enable), this);

    // T64: section colour family is the band's identity (pink/amber/yellow),
    // resolved by the LookAndFeel through the parent chain.
    dopamine::applySection (*this, dopamine::sectionForBand (bandIndex));

    // ---- Band Effects (T49; T62 keeps the title, drops the SB section) ----
    addSubtitle (jsTitle, "Band Effects", dopamine::Role::depth);
    addKnob (jitDepth,  pid (params::IDs::jitterDepth),   "Jit Depth",  dopamine::Role::depth);
    addKnob (jitRate,   pid (params::IDs::jitterRate),    "Jit Rate",   dopamine::Role::rate);
    addKnob (shimDepth, pid (params::IDs::shimmerDepth),  "Shim Depth", dopamine::Role::depth);
    addKnob (shimRate,  pid (params::IDs::shimmerRate),   "Shim Rate",  dopamine::Role::rate);
    // T50: visible label is Rough (was Fog); the parameter ID stays b{n}_fog
    // so existing sessions/automation keep working.
    addKnob (bandFog,   pid (params::IDs::fog),           "Rough",      dopamine::Role::depth); // T49 whole-band fog

    // ---- Vibration ----
    addSubtitle (vibTitle, "Vibration", dopamine::Role::rate);
    vibWaveLabel.setText ("Wave", juce::dontSendNotification);
    vibWaveLabel.setJustificationType (juce::Justification::centred);
    vibWaveLabel.setFont (juce::FontOptions (13.0f));
    vibWaveLabel.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::inkSoft());
    addAndMakeVisible (vibWaveLabel);
    vibWaveCombo.setComponentID (pid (params::IDs::vibWave));
    dopamine::applyRole (vibWaveCombo, dopamine::Role::rate);
    vibWaveCombo.addItemList ({ "Sine", "Square", "FullWave", "Noise" }, 1);   // matches vibWaveChoices
    addAndMakeVisible (vibWaveCombo);
    vibWaveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, pid (params::IDs::vibWave), vibWaveCombo);
    addKnob (vibDepth, pid (params::IDs::vibDepth), "Depth", dopamine::Role::depth);
    addKnob (vibRate,  pid (params::IDs::vibRate),  "Rate",  dopamine::Role::rate);
}

BandCardComponent::~BandCardComponent()
{
    apvts.removeParameterListener (
        params::makeBandParameterID (bandIndex, params::IDs::enable), this);
    cancelPendingUpdate();
}

//==============================================================================
void BandCardComponent::addKnob (Knob& knob, const juce::String& paramID,
                                 const juce::String& displayName, dopamine::Role role)
{
    knob.label.setText (displayName, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (13.0f)); // §7 parameter name
    knob.label.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::inkSoft());

    knob.slider.setComponentID (paramID);
    dopamine::applyRole (knob.slider, role);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18); // §7 size follow-up
    knob.slider.setColour (juce::Slider::textBoxTextColourId, NeoLookAndFeel::Tokens::ink());
    knob.slider.setColour (juce::Slider::textBoxBackgroundColourId, dopamine::transparent());
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, dopamine::transparent());
    knob.slider.setTextValueSuffix (NeoLookAndFeel::unitForParam (paramID));
    knob.slider.textFromValueFunction = [] (double v)
    {
        return NeoLookAndFeel::formatKnobValue (v);
    };

    addAndMakeVisible (knob.label);
    addAndMakeVisible (knob.slider);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, knob.slider);
    // SliderAttachment overwrites textFromValueFunction — re-apply (T37).
    knob.slider.textFromValueFunction = [] (double v)
    {
        return NeoLookAndFeel::formatKnobValue (v);
    };
    knob.slider.updateText();
}

void BandCardComponent::addSubtitle (juce::Label& label, const juce::String& text,
                                     dopamine::Role role)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setFont (juce::FontOptions (15.0f, juce::Font::bold)); // §7 section subtitle
    label.setColour (juce::Label::textColourId, dopamine::accent (role).active);
    // The soft strip is painted by BandCardComponent::paint as a full row —
    // no label background here.
    addAndMakeVisible (label);
}

//==============================================================================
void BandCardComponent::resized()
{
    // T63: the Band N title + Band On switch moved into the tab strip, so
    // the card starts with Band Effects. Content stays 400 px high; the rows
    // use 360 px, leaving breathing room at the bottom.
    auto area = getLocalBounds().reduced (6, 8);

    constexpr int gap = 8;
    constexpr int subH = 24;
    constexpr int knobH = 84;    // §7: textbox 18 + label 14 + knob face
    // T63 follow-up: Wave is no longer a full-width strip, but it spans the
    // Vibration knobs exactly (Depth's left edge to Rate's right edge).
    constexpr int waveRowH = 64;
    constexpr int waveComboH = 38;

    // Band Effects: 3-column grid; the second row leaves the third cell
    // empty so Shim Depth/Rate stay vertically aligned with Jit Depth/Rate.
    jsTitle.setBounds (area.removeFromTop (subH));
    jsSubRect = jsTitle.getBounds();
    jsTitle.setBounds (jsSubRect.reduced (14, 0));
    auto jsRow = area.removeFromTop (knobH);
    layKnobRow (jsRow, { &jitDepth, &jitRate, &bandFog }, 3);
    area.removeFromTop (gap);
    auto shimRow = area.removeFromTop (knobH);
    layKnobRow (shimRow, { &shimDepth, &shimRate }, 3);
    area.removeFromTop (gap);

    // Vibration: Wave spans from the visible Jit Depth disc's left edge to the
    // visible Rough disc's right edge (the knob faces themselves, not the
    // 3-column grid).
    vibTitle.setBounds (area.removeFromTop (subH));
    vibSubRect = vibTitle.getBounds();
    vibTitle.setBounds (vibSubRect.reduced (14, 0));
    auto waveRow = area.removeFromTop (waveRowH);
    auto vibRow = area.removeFromTop (knobH);
    layKnobRow (vibRow, { &vibDepth, &vibRate }, 3);

    const auto leftDisc = NeoLookAndFeel::getKnobDiscBounds (jitDepth.slider);
    const auto rightDisc = NeoLookAndFeel::getKnobDiscBounds (bandFog.slider);
    const auto waveX = juce::roundToInt (leftDisc.getX());
    const auto waveWidth = juce::roundToInt (rightDisc.getRight()) - waveX;
    vibWaveLabel.setBounds (waveRow.removeFromTop (16).withX (waveX).withWidth (waveWidth));
    vibWaveCombo.setBounds (waveX, waveRow.reduced (0, 5).getY(), waveWidth, waveComboH);
}

//==============================================================================
void BandCardComponent::parameterChanged (const juce::String&, float)
{
    // S1: APVTS listeners can fire on the audio thread.
    triggerAsyncUpdate();
}

void BandCardComponent::handleAsyncUpdate()
{
    updateGreyOut();
    repaint();
}

void BandCardComponent::layKnobRow (juce::Rectangle<int>& row,
                                    std::initializer_list<Knob*> knobs,
                                    int columns)
{
    // Fixed column width: rows with fewer knobs still use the SAME grid, so
    // every knob lines up vertically across sections (T62).
    const int cols = juce::jmax (1, columns);
    const int cellW = row.getWidth() / cols;
    for (auto* knob : knobs)
    {
        auto cell = row.removeFromLeft (cellW);
        auto top = cell.removeFromTop (14); // §7: 13 px label needs 14 px
        knob->label.setBounds (top);
        knob->slider.setBounds (cell.reduced (2, 2));
    }
}

//==============================================================================
void BandCardComponent::paint (juce::Graphics& g)
{
    // Grey-out sync on every paint so DAW automation is always reflected.
    updateGreyOut();

    const auto section = dopamine::sectionOf (*this);

    // The band zone is ONE rounded card (tab strip + content) painted by the
    // editor parent. No divider between the tab strip and the content — the
    // Growl/Master tabs are seamless too.
    // Section subtitle strips — flat tinted rows (no convex gradient) with a
    // dopamine accent pill. Text and pill are inset symmetrically.
    const auto drawStrip = [&g, section, on = bandOnState] (juce::Rectangle<int> r,
                                                            dopamine::Role role)
    {
        if (r.isEmpty())
            return;

        // The strip and its pill are painted here (not child components), so
        // they must follow the band state explicitly: a greyed card with
        // fully saturated section headers read as "still active".
        const auto strip = r.toFloat().reduced (4.0f, 2.0f);
        g.setColour (on ? dopamine::sectionStrip (section) : dopamine::disabledFace());
        g.fillRoundedRectangle (strip, 8.0f);

        const juce::Rectangle<float> pill (strip.getRight() - 24.0f,
                                           strip.getCentreY() - 1.5f, 14.0f, 3.0f);
        g.setColour (on ? dopamine::accent (role).base.withAlpha (0.9f)
                        : dopamine::disabledGlyph().withAlpha (0.5f));
        g.fillRoundedRectangle (pill, 1.5f);
    };

    drawStrip (jsSubRect, dopamine::Role::depth);
    drawStrip (vibSubRect, dopamine::Role::rate);
}

void BandCardComponent::updateGreyOut() noexcept
{
    const bool bandOn = apvts.getRawParameterValue (
        params::makeBandParameterID (bandIndex, params::IDs::enable))->load() > 0.5f;
    bandOnState = bandOn;

    // Everything except the band master switch follows it.
    const auto set = [bandOn] (juce::Component& c) { c.setEnabled (bandOn); };
    const std::initializer_list<juce::Component*> all =
    {
        &jsTitle, &vibTitle,
        &jitDepth.label,   &jitDepth.slider,
        &jitRate.label,    &jitRate.slider,
        &shimDepth.label,  &shimDepth.slider,
        &shimRate.label,   &shimRate.slider,
        &bandFog.label,    &bandFog.slider,
        &vibDepth.label,   &vibDepth.slider,
        &vibRate.label,    &vibRate.slider,
        &vibWaveLabel,     &vibWaveCombo
    };
    for (auto* c : all)
        set (*c);
}

} // namespace ui
