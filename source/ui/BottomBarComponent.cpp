#include "ui/BottomBarComponent.h"

#include "ui/NeoLookAndFeel.h"

#include <algorithm>

namespace ui
{

double BottomBarComponent::latencyMs (int samples, double sampleRate) noexcept
{
    return sampleRate > 0.0 ? static_cast<double> (samples) * 1000.0 / sampleRate : 0.0;
}

//==============================================================================
BottomBarComponent::BottomBarComponent (juce::AudioProcessorValueTreeState& apvts,
                                        ui::MeterSource& meter,
                                        PluginProcessor& processor)
    : apvts (apvts), meter (meter), processor (processor)
{
    dopamine::applySection (*this, dopamine::Section::master);
    dopamine::applySection (growlPage,  dopamine::Section::growl);
    dopamine::applySection (masterPage, dopamine::Section::master);

    growlPage.layoutCallback  = [this] { layoutGrowlPage(); };
    masterPage.layoutCallback = [this] { layoutMasterPage(); };

    addKnob (amountKnob, growlPage, params::IDs::growlAmount, "Amount", dopamine::Role::depth);
    addKnob (rateKnob,   growlPage, params::IDs::growlRate,   "Rate",   dopamine::Role::rate);
    addKnob (toneKnob,   growlPage, params::IDs::growlTone,   "Tone",   dopamine::Role::level);

    addKnob (formantKnob, masterPage, params::IDs::formantShift, "Formant Shift", dopamine::Role::frequency);
    addKnob (dryWetKnob,  masterPage, params::IDs::dryWet,       "Dry / Wet",     dopamine::Role::level);
    addKnob (trimKnob,    masterPage, params::IDs::outputTrim,   "Output Trim",   dopamine::Role::level);

    // T38 follow-up: no text on the switch (the 30 px body would truncate it).
    formantToggle.setButtonText ({});
    formantToggle.setComponentID (params::IDs::formantEnabled);
    formantToggle.setTitle ("Formant On");
    formantToggle.setDescription ("Enable the formant shift stage (adds 2048 samples of latency)");
    dopamine::applyRole (formantToggle, dopamine::Role::frequency);
    masterPage.addAndMakeVisible (formantToggle);
    formantToggleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, params::IDs::formantEnabled, formantToggle);

    // T66: Growl is the default page, Master is the second page. The tab bar
    // uses the shared Band TabBarButton drawing (30 px, equal widths, pills).
    masterTabs.setComponentID ("masterTabs");
    masterTabs.setOutline (0);
    masterTabs.setTabBarDepth (30);
    masterTabs.setColour (juce::TabbedComponent::backgroundColourId, dopamine::transparent());
    masterTabs.addTab ("Growl", dopamine::transparent(), &growlPage, false);
    masterTabs.addTab ("Master", dopamine::transparent(), &masterPage, false);
    addAndMakeVisible (masterTabs);
    masterTabs.setCurrentTabIndex (0);

    auto& tabBar = masterTabs.getTabbedButtonBar();
    if (auto* growlTab = tabBar.getTabButton (0))
    {
        dopamine::applySection (*growlTab, dopamine::Section::growl);
        growlTab->setWantsKeyboardFocus (true);
    }
    if (auto* masterTab = tabBar.getTabButton (1))
    {
        dopamine::applySection (*masterTab, dopamine::Section::master);
        masterTab->setWantsKeyboardFocus (true);
    }

    // User spec: the Formant Shift knob greys out while formant is disabled.
    // APVTS listeners can fire on the audio thread -> AsyncUpdater (S1 pattern).
    apvts.addParameterListener (params::IDs::formantEnabled, this);
    refreshFormantKnobEnabled();

    startTimerHz (30);
}

BottomBarComponent::~BottomBarComponent()
{
    stopTimer();
    apvts.removeParameterListener (params::IDs::formantEnabled, this);
}

//==============================================================================
void BottomBarComponent::refreshFormantKnobEnabled() noexcept
{
    const auto enabled = apvts.getRawParameterValue (params::IDs::formantEnabled)->load() > 0.5f;
    formantKnob.slider.setEnabled (enabled);
    formantKnob.label.setEnabled (enabled);
    repaint();
}

//==============================================================================
void BottomBarComponent::addKnob (Knob& knob, juce::Component& parent,
                                  const juce::String& paramID,
                                  const juce::String& displayName, dopamine::Role role)
{
    knob.label.setText (displayName, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (13.0f)); // §7 parameter name
    knob.label.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::inkSoft());

    knob.slider.setComponentID (paramID);
    dopamine::applyRole (knob.slider, role);

    knob.slider.setColour (juce::Slider::textBoxTextColourId, NeoLookAndFeel::Tokens::ink());
    knob.slider.setColour (juce::Slider::textBoxBackgroundColourId, dopamine::transparent());
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, dopamine::transparent());
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    knob.slider.setTextValueSuffix (NeoLookAndFeel::unitForParam (paramID));

    parent.addAndMakeVisible (knob.label);
    parent.addAndMakeVisible (knob.slider);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramID, knob.slider);

    // SliderAttachment OVERWRITES textFromValueFunction — re-apply after.
    if (paramID == params::IDs::growlRate)
    {
        knob.slider.textFromValueFunction = [] (double v)
        {
            return juce::String (v, 2); // two decimals; suffix "×" is appended
        };
    }
    else
    {
        knob.slider.textFromValueFunction = [] (double v)
        {
            return NeoLookAndFeel::formatKnobValue (v);
        };
    }
    knob.slider.updateText();
}

//==============================================================================
void BottomBarComponent::resized()
{
    // T66: tab bar 30 px + 90 px page content; meters live below the tabs.
    masterTabs.setBounds (0, 0, getWidth(), 120);

    const int rowWidth = getWidth() - 12;
    if (rowWidth <= 0)
    {
        meterL = meterR = {};
        return;
    }

    const int cellW = rowWidth / 3;
    const float sliderW = static_cast<float> (cellW - 4);
    const float barLeft = 6.0f + 2.0f + sliderW * 0.5f - 20.0f;
    const float barRight = 6.0f + 2.0f * static_cast<float> (cellW)
                         + 2.0f + sliderW * 0.5f + 20.0f;
    const float barW = juce::jmax (1.0f, barRight - barLeft);

    // Fixed to the Growl page's outer knob discs; both pages share this span.
    meterL = { barLeft, 137.0f, barW, 12.0f };
    meterR = { barLeft, 155.0f, barW, 12.0f };
}

//==============================================================================
void BottomBarComponent::layoutMasterPage()
{
    auto area = masterPage.getLocalBounds().reduced (6, 0);
    area.removeFromTop (6);
    auto row = area.removeFromTop (84);

    const int cellW = row.getWidth() / 3;
    if (cellW <= 0)
        return;

    // Formant: full-cell label, 72 px slider centred in the cell, toggle to
    // the right of the centred disc (T66 approved layout).
    {
        auto cell = row.removeFromLeft (cellW);
        formantKnob.label.setBounds (cell.removeFromTop (14));
        auto sliderZone = cell.reduced (0, 2).withSizeKeepingCentre (72, 66);
        formantKnob.slider.setBounds (sliderZone);

        const auto disc = NeoLookAndFeel::getKnobDiscBounds (formantKnob.slider);
        formantToggle.setBounds (juce::roundToInt (disc.getRight()) + 4,
                                 juce::roundToInt (disc.getCentreY()) - 10,
                                 30, 20);
    }

    for (auto* knob : { &dryWetKnob, &trimKnob })
    {
        auto cell = row.removeFromLeft (cellW);
        knob->label.setBounds (cell.removeFromTop (14));
        knob->slider.setBounds (cell.reduced (2, 2));
    }
}

void BottomBarComponent::layoutGrowlPage()
{
    auto area = growlPage.getLocalBounds().reduced (6, 0);
    area.removeFromTop (6);
    auto row = area.removeFromTop (84);

    const int cellW = row.getWidth() / 3;
    if (cellW <= 0)
        return;

    for (auto* knob : { &amountKnob, &rateKnob, &toneKnob })
    {
        auto cell = row.removeFromLeft (cellW);
        knob->label.setBounds (cell.removeFromTop (14));
        knob->slider.setBounds (cell.reduced (2, 2));
    }
}

//==============================================================================
void BottomBarComponent::timerCallback()
{
    float left = 0.0f, right = 0.0f;
    if (meter.readLatest (left, right))
    {
        leftPeak = left;
        rightPeak = right;
    }
    repaint();
}

//==============================================================================
void BottomBarComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto section = dopamine::sectionOf (*this);
    const auto levelAccent = dopamine::accent (dopamine::Role::level);

    NeoLookAndFeel::drawNeoPanel (g, bounds, NeoLookAndFeel::Tokens::radiusMedium,
                                  section, false);

    if (meterL.getWidth() <= 1.0f)
        return;

    const auto drawBar = [&g, section, levelAccent] (const juce::Rectangle<float>& bar,
                                                      float peak, const juce::String& label)
    {
        const auto value = juce::jlimit (0.0f, 1.0f, peak);

        NeoLookAndFeel::drawNeoInset (g, bar, 6.0f, section);

        auto fill = bar.reduced (1.0f);
        fill.setWidth (juce::jmax (0.0f, (bar.getWidth() - 2.0f) * value));
        if (fill.getWidth() > 0.5f)
        {
            g.setColour (levelAccent.base.withAlpha (0.74f));
            g.fillRoundedRectangle (fill, 5.0f);
        }

        g.setFont (juce::FontOptions (11.0f));
        g.setColour (NeoLookAndFeel::Tokens::inkSoft());
        g.drawText (label,
                    juce::Rectangle<float> (bar.getX() - 22.0f, bar.getY(), 16.0f, bar.getHeight()),
                    juce::Justification::centredRight, false);
    };

    drawBar (meterL, leftPeak, "L");
    drawBar (meterR, rightPeak, "R");
}

} // namespace ui
