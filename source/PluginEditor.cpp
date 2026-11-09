#include "PluginEditor.h"

#include <juce_data_structures/juce_data_structures.h>

#include "ui/NeoLookAndFeel.h"
#include "util/UserData.h"

namespace
{
//==============================================================================
//  T41/T42: UI-scale persistence (per user, NOT per project). Message thread
//  only. A missing/unreadable settings file silently falls back to 1.0.
//  The path itself lives in util::UserData (shared with the preset library).
//==============================================================================
juce::PropertiesFile::Options uiSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName     = "NeoVoiDrive";
    options.folderName          = "NeoVoiDrive";
    options.filenameSuffix      = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers    = false;
    options.ignoreCaseOfKeyNames = true;
    options.storageFormat       = juce::PropertiesFile::storeAsXML;
    return options;
}

float loadSavedUiScale()
{
    juce::PropertiesFile props (util::UserData::getSettingsFile(), uiSettingsOptions());
    return static_cast<float> (props.getDoubleValue ("uiScale", 1.0));
}

void saveUiScale (float scale)
{
    juce::PropertiesFile props (util::UserData::getSettingsFile(), uiSettingsOptions());
    props.setValue ("uiScale", static_cast<double> (scale));
    props.saveIfNeeded();
}
} // namespace

//==============================================================================
//  T41: scaled canvas
//==============================================================================
void PluginEditor::Content::paint (juce::Graphics& g)
{
    if (owner != nullptr)
        owner->paintContent (g, *this);
}

void PluginEditor::Content::resized()
{
    if (owner != nullptr)
        owner->layoutContent (*this);
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), topBar (p),
      crossoverDisplay (p.apvts, p.getSpectrumSource(), p.getSpectrumSourceOut()),
      bandCard1 (p.apvts, 1), bandCard2 (p.apvts, 2), bandCard3 (p.apvts, 3),
      bottomBar (p.apvts, p.getMeterSource(), p)
{
    // T15/M4: the theme lives on this editor's root, NOT the process-global
    // L&F slot — multiple editor instances would fight over the global and
    // one closing would strip the theme from the others. Children inherit
    // via the parent chain; top-level popups set their own L&F explicitly.
    setLookAndFeel (&neoLookAndFeel);

    // T41: all zones live inside the scaled canvas.
    content.owner = this;
    addAndMakeVisible (content);

    // T16: top zone (Logo / Bypass / Preset / Settings).
    content.addAndMakeVisible (topBar);

    // T17: spectrum + crossover zone.
    content.addAndMakeVisible (crossoverDisplay);

    // T38: three band effect cards in tabs (count follows band_count).
    // UI_STYLE_GUIDE follow-up: no rectangle outline around the tab content
    // (user report: a rectangular frame inside the card zone) — the rounded
    // card face is drawn by the BandCard itself.
    bandTabs.setComponentID ("bandTabs");
    bandTabs.setOutline (0);
    content.addAndMakeVisible (bandTabs);

    // T19: bottom bar (formant / dry-wet / trim / meters / latency).
    content.addAndMakeVisible (bottomBar);

    // T18/T38: the tab set follows the band count (F12 / R12 — the cards
    // stay alive as members, tab removal never deletes them).
    p.apvts.addParameterListener (params::IDs::bandCount, this);
    updateBandTabs();

    // T62: the Sideband Low Cut knob (and therefore its live envelope
    // preview) is hidden with the rest of the Sideband UI. The preview
    // renderer stays dormant in CrossoverDisplay for future restoration.

    // T41: the Settings dialog drives the user scale through these callbacks.
    topBar.getUserScale = [this] { return userScale; };
    topBar.setUserScale = [this] (float scale) { setUiScale (scale); };

    // T41: restore the saved user scale (default 1.0 = the compact two-column
    // canvas; T63 resized the design canvas to 1200 x 720).
    setUiScale (juce::jlimit (minUserScale, maxUserScale, loadSavedUiScale()), false);
}

PluginEditor::~PluginEditor()
{
    // T18: stop listening before the processor (and its apvts) goes away.
    processorRef.apvts.removeParameterListener (params::IDs::bandCount, this);

    // M4: drop the root L&F (instance-scoped; nothing global to restore).
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::Point<int> PluginEditor::sizeForUserScale (float scale) noexcept
{
    const float k = defaultScaleBase * juce::jlimit (minUserScale, maxUserScale, scale);
    return { juce::roundToInt (designWidth * k), juce::roundToInt (designHeight * k) };
}

void PluginEditor::setUiScale (float scale, bool persist)
{
    userScale = juce::jlimit (minUserScale, maxUserScale, scale);

    // Popup menus (Preset, wave/slope combos) are separate top-level windows
    // and do not inherit the canvas transform; scale them explicitly.
    neoLookAndFeel.setUiScale (userScale);

    const auto size = sizeForUserScale (userScale);
    setSize (size.x, size.y);   // resized() re-derives the AffineTransform

    // Only a window that hosts THIS editor as its content should be resized
    // directly (setContentComponentSize accounts for its title bar/borders).
    // The JUCE Standalone puts a content wrapper (with an optional 30 px
    // "input muted" strip) between the window and the editor and already
    // follows the editor from a ComponentListener; forcing the raw editor
    // size into that wrapper clipped the bottom of the UI on every re-adjust.
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        if (window->getContentComponent() == this)
            window->setContentComponentSize (size.x, size.y);

    if (persist)
        saveUiScale (userScale);
}

void PluginEditor::paint (juce::Graphics& g)
{
    // Opaque background (also covers any letterbox area if a host forces a
    // width that does not match the design aspect).
    g.fillAll (neoLookAndFeel.findColour (juce::ResizableWindow::backgroundColourId));
}

void PluginEditor::paintContent (juce::Graphics& g, const juce::Component& canvas)
{
    // Fill the canvas (the editor also fills its own bounds, so a transformed
    // canvas never shows the window background through the cards).
    g.fillAll (neoLookAndFeel.findColour (juce::ResizableWindow::backgroundColourId));

    // UI_STYLE_GUIDE §4 shadowCard: the card zones cast their soft shadow
    // from the PARENT — children are clipped to their own bounds and cannot
    // blur into the gutters (§8-D). Pre-rendered + cached inside
    // drawNeoShadowPair (T64: white top-left + hue-dark bottom-right).
    //
    // The band zone is ONE rounded card spanning the tab strip AND the
    // content — so the canvas paints both its shadow and its face here.
    juce::ignoreUnused (canvas);
    const auto radius = ui::NeoLookAndFeel::Tokens::radiusMedium;
    ui::NeoLookAndFeel::drawNeoShadowPair (g, topBar.getBounds().toFloat().reduced (0.5f),
                                           radius, ui::dopamine::Section::global);
    ui::NeoLookAndFeel::drawNeoShadowPair (g, crossoverDisplay.getBounds().toFloat().reduced (0.5f),
                                           radius, ui::dopamine::Section::spectrum);
    ui::NeoLookAndFeel::drawNeoShadowPair (g, bandTabs.getBounds().toFloat().reduced (0.5f),
                                           radius, ui::dopamine::Section::global);
    ui::NeoLookAndFeel::drawNeoShadowPair (g, bottomBar.getBounds().toFloat().reduced (0.5f),
                                           radius, ui::dopamine::Section::master);
    ui::NeoLookAndFeel::drawNeoPanel (g, bandTabs.getBounds().toFloat().reduced (0.5f),
                                      radius, ui::dopamine::Section::global, false);
}

void PluginEditor::resized()
{
    // T41/T62: the canvas is always authored at design size; the editor
    // applies a uniform scale derived from its width (height follows the
    // design aspect because the UI-scale control sets both). A host that
    // forces a different width still gets a proportional, correctly
    // anchored UI.
    const float k = juce::jlimit (0.5f, 2.5f,
                                  static_cast<float> (getWidth()) / static_cast<float> (designWidth));
    content.setBounds (0, 0, designWidth, designHeight);
    content.setTransform (juce::AffineTransform::scale (k));
}

void PluginEditor::layoutContent (juce::Component& canvas)
{
    // User spec: spacing between components and from the window edge.
    constexpr int edge = 14;
    constexpr int gutter = 12;
    auto area = canvas.getLocalBounds().reduced (edge);

    // T16/T62: header stays full width on top.
    topBar.setBounds (area.removeFromTop (50));
    area.removeFromTop (gutter);

    // T63/T73: two-column main area — left 70 % (812 px), right 30 % (348 px).
    // The gutter is excluded from the split so 812 + 12 + 348 = 1172.
    const auto columnsWidth = area.getWidth() - gutter;
    const auto leftWidth = juce::roundToInt (static_cast<float> (columnsWidth) * 0.70f);
    auto leftColumn = area.removeFromLeft (leftWidth);
    area.removeFromLeft (gutter);
    auto rightColumn = area;

    // Left: spectrum + crossover controls (curve width/height grow with the
    // new canvas; the bottom knob row reflows inside 812 px).
    crossoverDisplay.setBounds (leftColumn);

    // Right: Band tabs on top, Master panel at the bottom. The canvas height
    // reduction comes only from Master (228 -> 188); the Band card keeps its
    // full 430 px tab / 400 px content height.
    constexpr int masterHeight = 188;
    bottomBar.setBounds (rightColumn.removeFromBottom (masterHeight));
    rightColumn.removeFromBottom (gutter);
    bandTabs.setBounds (rightColumn);
    bandTabs.setTabBarDepth (30);   // §7: 14 px tab text
}

//==============================================================================
void PluginEditor::parameterChanged (const juce::String& parameterID, float)
{
    // S1 (review): APVTS listeners fire on the thread that wrote the
    // parameter — host automation can do that on the audio thread, where
    // setEnabled/repaint are forbidden. Defer to the message thread.
    if (parameterID == params::IDs::bandCount)
        triggerAsyncUpdate();
}

void PluginEditor::handleAsyncUpdate()
{
    updateBandTabs();
}

void PluginEditor::updateBandTabs()
{
    const auto* param = processorRef.apvts.getParameter (params::IDs::bandCount);
    const int count = param != nullptr
                    ? juce::roundToInt (param->getValue() * 2.0f) + 1
                    : 3;

    if (bandTabs.getNumTabs() == count)
        return;

    // T38: rebuild the tab set. The cards are members and were added with
    // deleteWhenRemoved = false, so clearTabs() only drops the tabs.
    // UI_STYLE_GUIDE: the band zone is ONE rounded card painted by the
    // editor — both the TabbedComponent background and the per-tab content
    // background stay transparent so the card face shows through.
    //
    // T63: the per-tab Band On switches are owned by their TabBarButtons
    // (extra components), so drop the attachments before clearTabs() deletes
    // those buttons.
    for (auto& attachment : bandEnableAttachments)
        attachment.reset();
    bandEnableToggles.fill (nullptr);

    // Audit P3: clearTabs() resets the selection to Band 1; keep the user's
    // band when it still exists (clamped when the count shrinks).
    const int previousIndex = juce::jmax (0, bandTabs.getCurrentTabIndex());

    bandTabs.clearTabs();
    ui::BandCardComponent* cards[3] = { &bandCard1, &bandCard2, &bandCard3 };
    for (int i = 0; i < count; ++i)
    {
        bandTabs.addTab ("Band " + juce::String (i + 1),
                         ui::dopamine::transparent(),
                         cards[i], false);

        auto bandSwitch = std::make_unique<BandEnableSwitch>();
        bandSwitch->toggle.setButtonText ({});
        bandSwitch->toggle.setComponentID (
            params::makeBandParameterID (i + 1, params::IDs::enable));
        bandSwitch->toggle.setTitle ("Band " + juce::String (i + 1) + " On");
        bandSwitch->toggle.setDescription (
            "Enable or bypass all effects for band " + juce::String (i + 1));
        bandSwitch->setSize (46, 20);
        ui::dopamine::applySection (*bandSwitch, ui::dopamine::sectionForBand (i + 1));

        auto* rawToggle = &bandSwitch->toggle;
        auto& buttonBar = bandTabs.getTabbedButtonBar();
        if (auto* tabButton = buttonBar.getTabButton (i))
        {
            // Batch 2: JUCE tab buttons default to non-focusable; enable it so
            // the shared focus ring is reachable by keyboard.
            tabButton->setWantsKeyboardFocus (true);
            ui::dopamine::applySection (*tabButton, ui::dopamine::sectionForBand (i + 1));
            tabButton->setExtraComponent (
                bandSwitch.release(), juce::TabBarButton::beforeText);
            bandEnableToggles[static_cast<size_t> (i)] = rawToggle;
            bandEnableAttachments[static_cast<size_t> (i)] =
                std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    processorRef.apvts,
                    params::makeBandParameterID (i + 1, params::IDs::enable),
                    *rawToggle);
        }
    }
    bandTabs.setCurrentTabIndex (juce::jmin (previousIndex, count - 1));
}
