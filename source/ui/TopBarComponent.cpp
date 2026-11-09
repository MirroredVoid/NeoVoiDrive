#include "ui/TopBarComponent.h"
#include "ui/BottomBarComponent.h"
#include "ui/NeoLookAndFeel.h"
#include "util/PresetBank.h"
#include "util/PresetIO.h"
#include "util/UserData.h"


namespace ui
{

namespace
{
// T42 preset menu IDs: factory items keep 1..numPresets; user presets and the
// custom-preset actions live in non-overlapping ranges.
constexpr int presetUserBase = 1001;
constexpr int presetExportId = 9001;
constexpr int presetImportId = 9002;
constexpr int presetFolderId = 9003;
} // namespace

//==============================================================================
juce::String TopBarComponent::formatLatencyText (int samples, double sampleRate)
{
    const auto ms = BottomBarComponent::latencyMs (samples, sampleRate);
    return juce::String (samples) + " samples / " + juce::String (ms, 2) + " ms";
}

juce::Point<int> TopBarComponent::settingsSizeForScale (float scale) noexcept
{
    return SettingsDialog::sizeForScale (scale);
}

//==============================================================================
//  Settings dialog (quefrency / framesize / latency / About)
//==============================================================================
TopBarComponent::SettingsDialog::SettingsDialog (juce::AudioProcessorValueTreeState& apvtsRef,
                                                 float userScale,
                                                 std::function<void (float)> onScaleChange,
                                                 std::function<int()> latencyProvider,
                                                 double sr)
    : apvts (apvtsRef), onUiScaleChange (std::move (onScaleChange)),
      getLatencySamples (std::move (latencyProvider)), sampleRate (sr)
{
    setLookAndFeel (&theme); // M4: instance-scoped theme for this popup
    dopamine::applySection (*this, dopamine::Section::settings);

    // Batch 2: same section-strip language as the Band/Master cards.
    const auto setupStrip = [] (juce::Label& label, const juce::String& text,
                                dopamine::Role role)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, dopamine::accent (role).active);
    };
    setupStrip (formantStrip,   "Formant",   dopamine::Role::frequency);
    setupStrip (monitorStrip,   "Monitor",   dopamine::Role::global);
    setupStrip (interfaceStrip, "Interface", dopamine::Role::global);
    setupStrip (aboutStrip,     "About",     dopamine::Role::global);

    quefrencyLabel.setText ("Quefrency", juce::dontSendNotification);
    quefrencyLabel.setJustificationType (juce::Justification::centred);
    quefrencyLabel.setFont (juce::FontOptions (13.0f));
    quefrencyLabel.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::inkSoft());

    quefrencySlider.setRange (0.5, 5.0, 0.01);
    dopamine::applyRole (quefrencySlider, dopamine::Role::frequency);
    // Explicit colours (user report: white-on-white in this dialog) + the
    // shared knob value format (<=3 digits + unit) and no outline box.
    quefrencySlider.setColour (juce::Slider::textBoxTextColourId, NeoLookAndFeel::Tokens::ink());
    quefrencySlider.setColour (juce::Slider::textBoxBackgroundColourId, dopamine::transparent());
    quefrencySlider.setColour (juce::Slider::textBoxOutlineColourId, dopamine::transparent());
    quefrencySlider.setTextValueSuffix (NeoLookAndFeel::unitForParam ("formant_quefrency"));
    quefrencySlider.textFromValueFunction = [] (double v)
    {
        return NeoLookAndFeel::formatKnobValue (v);
    };
    quefrencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "formant_quefrency", quefrencySlider);

    // SliderAttachment OVERWRITES textFromValueFunction — re-apply after.
    quefrencySlider.textFromValueFunction = [] (double v)
    {
        return NeoLookAndFeel::formatKnobValue (v);
    };
    quefrencySlider.updateText(); // refresh the textbox (filled by the attachment)

    framesizeLabel.setText ("FFT Frame Size", juce::dontSendNotification);
    framesizeLabel.setJustificationType (juce::Justification::centred);
    framesizeLabel.setFont (juce::FontOptions (13.0f));
    framesizeLabel.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::inkSoft());

    framesizeCombo.addItem (juce::String (TopBarComponent::formantFrameSize), 1); // T68 fixed
    framesizeCombo.setSelectedId (1, juce::dontSendNotification);
    framesizeCombo.setEnabled (false); // SPEC §3 F11: reserved slot, disabled
    dopamine::applyRole (framesizeCombo, dopamine::Role::frequency);

    // T66: read-only latency, moved out of the Master card.
    latencyLabel.setText ("Latency", juce::dontSendNotification);
    latencyLabel.setJustificationType (juce::Justification::centred);
    latencyLabel.setFont (juce::FontOptions (13.0f));
    latencyLabel.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::inkSoft());

    latencyValue.setComponentID ("settingsLatency");
    latencyValue.setJustificationType (juce::Justification::centred);
    latencyValue.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    latencyValue.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::ink());
    latencyValue.setText (formatLatencyText (0, sampleRate), juce::dontSendNotification);

    // T62: the Engine selector is hidden together with the Sideband UI.
    // engine_version remains an APVTS parameter (presets / automation).

    // T41/T63: UI size — a UI-only setting (not an APVTS parameter; persisted
    // per user in NeoVoiDrive.settings). 100 % = 1020x612 on the two-column
    // canvas; the old pre-T41 770x940 canvas is no longer the reference.
    uiScaleLabel.setText ("UI Size", juce::dontSendNotification);
    uiScaleLabel.setJustificationType (juce::Justification::centred);
    uiScaleLabel.setFont (juce::FontOptions (13.0f));
    uiScaleLabel.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::inkSoft());

    const int scalePercent[] = { 75, 80, 90, 100, 110, 125, 150 };
    for (int pct : scalePercent)
        uiScaleCombo.addItem (juce::String (pct) + " %"
                                  + (pct == 100 ? " (default)" : ""),
                              pct);
    {
        int nearestId = 100;
        int bestDelta = 1000;
        const int currentPct = juce::roundToInt (userScale * 100.0f);
        for (int pct : scalePercent)
            if (std::abs (pct - currentPct) < bestDelta)
            {
                bestDelta = std::abs (pct - currentPct);
                nearestId = pct;
            }
        uiScaleCombo.setSelectedId (nearestId, juce::dontSendNotification);
    }
    uiScaleCombo.setComponentID ("uiScaleCombo");
    dopamine::applyRole (uiScaleCombo, dopamine::Role::global);
    uiScaleCombo.onChange = [this]
    {
        const int pct = uiScaleCombo.getSelectedId();
        if (pct > 0)
        {
            const auto scale = static_cast<float> (pct) / 100.0f;
            applyScale (scale); // resize this dialog with its contents
            if (onUiScaleChange != nullptr)
                onUiScaleChange (scale);
        }
    };

    // User-facing About identity, driven by the build metadata (CMake
    // PRODUCT_NAME / VERSION / COMPANY_NAME) so it cannot drift (audit P3).
    aboutLabel.setText (juce::String (JucePlugin_Name) + " " + JucePlugin_VersionString
                        + " by " + JucePlugin_Manufacturer + "\n"
                        "Voice micro-tuning & formant shaper.\n"
                        "JUCE " JUCE_STRINGIFY(JUCE_MAJOR_VERSION) "."
                        JUCE_STRINGIFY(JUCE_MINOR_VERSION) "\n"
                        "Engine: Legacy (default). Sideband presets retained.",
                        juce::dontSendNotification);
    aboutLabel.setJustificationType (juce::Justification::topLeft);
    aboutLabel.setFont (juce::FontOptions (13.0f)); // batch 2: same body scale as the panel labels
    aboutLabel.setColour (juce::Label::textColourId, NeoLookAndFeel::Tokens::inkSoft());

    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (formantStrip);
    canvas.addAndMakeVisible (monitorStrip);
    canvas.addAndMakeVisible (interfaceStrip);
    canvas.addAndMakeVisible (aboutStrip);
    canvas.addAndMakeVisible (quefrencyLabel);
    canvas.addAndMakeVisible (quefrencySlider);
    canvas.addAndMakeVisible (framesizeLabel);
    canvas.addAndMakeVisible (framesizeCombo);
    canvas.addAndMakeVisible (latencyLabel);
    canvas.addAndMakeVisible (latencyValue);
    canvas.addAndMakeVisible (uiScaleLabel);
    canvas.addAndMakeVisible (uiScaleCombo);
    canvas.addAndMakeVisible (aboutLabel);

    startTimerHz (10);
    timerCallback();

    // T72: match the main UI immediately (the dialog is its own top-level
    // window, so it needs an explicit size instead of inheriting the editor's
    // AffineTransform).
    applyScale (userScale);
}

void TopBarComponent::SettingsDialog::timerCallback()
{
    const int samples = getLatencySamples != nullptr ? getLatencySamples() : 0;
    latencyValue.setText (TopBarComponent::formatLatencyText (samples, sampleRate),
                          juce::dontSendNotification);
}

juce::Point<int> TopBarComponent::SettingsDialog::sizeForScale (float scale) noexcept
{
    const auto s = juce::jlimit (0.5f, 2.5f, scale);
    return { juce::roundToInt (static_cast<float> (designWidth) * s),
             juce::roundToInt (static_cast<float> (designHeight) * s) };
}

void TopBarComponent::SettingsDialog::applyScale (float scale)
{
    uiScale = juce::jlimit (0.5f, 2.5f, scale);
    theme.setUiScale (uiScale); // combo popups are separate top-level windows
    const auto size = sizeForScale (uiScale);
    setSize (size.x, size.y);   // DialogWindow follows its content size
}

void TopBarComponent::SettingsDialog::resized()
{
    canvas.setBounds (0, 0, designWidth, designHeight);
    canvas.setTransform (juce::AffineTransform::scale (uiScale));
}

void TopBarComponent::SettingsDialog::layoutCanvas()
{
    // Batch 2: main-UI row language — section strip (24) + 13 px label (14)
    // + control. The large Quefrency knob stays, slightly smaller (84 px).
    // T72: layout is always in the 340 x 496 DESIGN space (the canvas
    // applies the scale transform); using this dialog's scaled bounds here
    // would overflow the canvas at >100 %.
    auto area = canvas.getLocalBounds().reduced (16);

    const auto takeStrip = [&area] (juce::Label& label, juce::Rectangle<int>& rect)
    {
        rect = area.removeFromTop (24);
        label.setBounds (rect.reduced (14, 0));
    };

    takeStrip (formantStrip, formantStripRect);
    quefrencyLabel.setBounds (area.removeFromTop (14));
    quefrencySlider.setBounds (area.removeFromTop (84));
    area.removeFromTop (12);

    framesizeLabel.setBounds (area.removeFromTop (14));
    framesizeCombo.setBounds (area.removeFromTop (32));
    area.removeFromTop (16);

    // T66: latency moved here from the Master card.
    takeStrip (monitorStrip, monitorStripRect);
    latencyLabel.setBounds (area.removeFromTop (14));
    latencyValue.setBounds (area.removeFromTop (20));
    area.removeFromTop (16);

    // T62: no Engine row — UI Size follows the Monitor section.
    // T41: UI size (75–150 %, 100 % = compact default).
    takeStrip (interfaceStrip, interfaceStripRect);
    uiScaleLabel.setBounds (area.removeFromTop (14));
    uiScaleCombo.setBounds (area.removeFromTop (32));
    area.removeFromTop (16);

    takeStrip (aboutStrip, aboutStripRect);
    aboutLabel.setBounds (area);
}

void TopBarComponent::SettingsDialog::paint (juce::Graphics& g)
{
    g.fillAll (dopamine::surface (dopamine::Surface::canvas));
}

void TopBarComponent::SettingsDialog::paintCanvas (juce::Graphics& g)
{
    // Canvas background + flat dialog panel (same 24 px card radius as the
    // main UI); no 1 px window border.
    g.fillAll (dopamine::surface (dopamine::Surface::canvas));
    const auto face = canvas.getLocalBounds().toFloat().reduced (12.0f);
    NeoLookAndFeel::drawNeoPanel (g, face, NeoLookAndFeel::Tokens::radiusMedium,
                                  dopamine::Section::settings, true);

    // Section strips: flat tinted row + role pill (same as Band/Master).
    const auto drawStrip = [&g] (juce::Rectangle<int> r, dopamine::Role role)
    {
        if (r.isEmpty())
            return;

        const auto strip = r.toFloat().reduced (4.0f, 2.0f);
        g.setColour (dopamine::sectionStrip (dopamine::Section::settings));
        g.fillRoundedRectangle (strip, 8.0f);

        const juce::Rectangle<float> pill (strip.getRight() - 24.0f,
                                           strip.getCentreY() - 1.5f, 14.0f, 3.0f);
        g.setColour (dopamine::accent (role).base.withAlpha (0.9f));
        g.fillRoundedRectangle (pill, 1.5f);
    };
    drawStrip (formantStripRect, dopamine::Role::frequency);
    drawStrip (monitorStripRect, dopamine::Role::global);
    drawStrip (interfaceStripRect, dopamine::Role::global);
    drawStrip (aboutStripRect, dopamine::Role::global);
}

//==============================================================================
//  TopBar
//==============================================================================
TopBarComponent::TopBarComponent (PluginProcessor& processorRef)
    : processor (processorRef)
{
    dopamine::applySection (*this, dopamine::Section::global);

    logoLabel.setText (JucePlugin_Name, juce::dontSendNotification);
    logoLabel.setFont (juce::FontOptions (21.0f, juce::Font::bold)); // §7 logo
    logoLabel.setJustificationType (juce::Justification::centredLeft);

    bypassButton.setButtonText ("Bypass");
    bypassButton.setComponentID ("bypassButton");
    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::textColourOffId, NeoLookAndFeel::Tokens::ink());
    bypassButton.setColour (juce::TextButton::textColourOnId, NeoLookAndFeel::Tokens::onAccent());
    dopamine::applyRole (bypassButton, dopamine::Role::global);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "bypass", bypassButton);

    presetButton.setButtonText ("Preset");
    presetButton.onClick = [this] { showPresetMenu(); };
    dopamine::applyRole (presetButton, dopamine::Role::global);

    settingsButton.setButtonText ("Settings");
    settingsButton.onClick = [this] { showSettings(); };
    dopamine::applyRole (settingsButton, dopamine::Role::global);

    addAndMakeVisible (logoLabel);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (presetButton);
    addAndMakeVisible (settingsButton);
}

TopBarComponent::~TopBarComponent()
{
    // T42: cancel a pending preset file dialog before the processor/apvts go
    // away (its callback captures a SafePointer and will be a no-op).
    presetFileChooser.reset();

    // If a Settings dialog is still open, unbind its APVTS attachment NOW
    // (synchronous): the DialogWindow is deleted asynchronously by the modal
    // manager, which may run after the processor's apvts is gone.
    if (auto* dialog = dynamic_cast<SettingsDialog*> (settingsContent.getComponent()))
        dialog->detachFromApvts();

    if (settingsContent != nullptr)
        if (auto* window = settingsContent->findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);

    // The preset menu holds a RAW pointer to our L&F (setLookAndFeel in
    // showPresetMenu); if the editor dies while the menu is open, the menu's
    // repaint would dereference a dangling L&F — dismiss any active menu
    // (review should-fix).
    juce::PopupMenu::dismissAllActiveMenus();
}

//==============================================================================
void TopBarComponent::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    logoLabel.setBounds (bounds.removeFromLeft (200));

    constexpr int buttonWidth = 96;
    auto buttons = bounds.removeFromRight (buttonWidth * 3 + 16);
    settingsButton.setBounds (buttons.removeFromRight (buttonWidth));
    buttons.removeFromRight (8);
    presetButton.setBounds (buttons.removeFromRight (buttonWidth));
    buttons.removeFromRight (8);
    bypassButton.setBounds (buttons.removeFromRight (buttonWidth));
}

void TopBarComponent::paint (juce::Graphics& g)
{
    // Flat header panel; the soft shadow pair is painted by the editor parent.
    NeoLookAndFeel::drawNeoPanel (g, getLocalBounds().toFloat(),
                                  NeoLookAndFeel::Tokens::radiusMedium,
                                  dopamine::Section::global, false);
}

//==============================================================================
void TopBarComponent::showPresetMenu()
{
    // Message-thread callback (UI): preset loading never touches the audio
    // thread (ARCHITECTURE §2).
    juce::PopupMenu menu;
    // Popup menus render in their own window which does NOT inherit the
    // component tree's L&F — without this the preset menu falls back to the
    // JUCE default grey (user report: style lost). JUCE 9: setLookAndFeel().
    // LIFETIME CONTRACT: setLookAndFeel stores a raw pointer; the editor's
    // L&F must outlive the menu. ~TopBarComponent() dismisses active menus,
    // and the menu callback guards with SafePointer (review).
    menu.setLookAndFeel (&getLookAndFeel());

    // T69: factory presets are grouped for scanning. The underlying bank order
    // stays unchanged, so item IDs remain `presetIndex + 1`.
    bool firstCategory = true;
    for (const auto category : { util::PresetBank::Category::voiceDrive,
                                 util::PresetBank::Category::soundEffects })
    {
        if (! firstCategory)
            menu.addSeparator();
        firstCategory = false;

        menu.addSectionHeader (util::PresetBank::getCategoryName (category));
        for (int i = 0; i < util::PresetBank::numPresets; ++i)
            if (util::PresetBank::getPresetCategory (i) == category)
                menu.addItem (i + 1, util::PresetBank::getPresetName (i));
    }

    // T42: the user library is scanned every time the menu opens, so a preset
    // exported a moment ago shows up immediately.
    const auto userFiles = util::PresetIO::listUserPresets();
    juce::PopupMenu userMenu;
    if (userFiles.isEmpty())
    {
        userMenu.addItem (-1, "(no user presets yet)", false);
    }
    else
    {
        for (int i = 0; i < userFiles.size(); ++i)
            userMenu.addItem (presetUserBase + i, util::PresetIO::nameFromFile (userFiles[i]));
    }
    menu.addSeparator();
    menu.addSubMenu ("User Presets", userMenu);

    menu.addSeparator();
    menu.addItem (presetExportId, "Export Preset...");
    menu.addItem (presetImportId, "Import Preset...");
    menu.addItem (presetFolderId, "Show Preset Folder");

    const juce::Component::SafePointer<TopBarComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
                        [safeThis, userFiles] (int result)
                        {
                            if (safeThis == nullptr || result == 0)
                                return;

                            if (result >= 1 && result <= util::PresetBank::numPresets)
                            {
                                util::PresetBank::loadPreset (safeThis->processor.apvts, result - 1);
                                // Audit P1-2: factory presets write the APVTS
                                // directly (no applyStateXml), so publish the
                                // new latency now for the stopped-state PDC /
                                // Settings readout.
                                safeThis->processor.refreshLatencyReport();
                                return;
                            }

                            if (result >= presetUserBase && result < presetExportId)
                            {
                                const int index = result - presetUserBase;
                                if (juce::isPositiveAndBelow (index, userFiles.size()))
                                    safeThis->loadUserPreset (userFiles[index]);
                                return;
                            }

                            if (result == presetExportId)
                                safeThis->exportPresetDialog();
                            else if (result == presetImportId)
                                safeThis->importPresetDialog();
                            else if (result == presetFolderId)
                                safeThis->openPresetFolder();
                        });
}

//==============================================================================
void TopBarComponent::loadUserPreset (const juce::File& file)
{
    juce::String error;
    if (! util::PresetIO::load (processor, file, error))
        showPresetError ("Import Preset", error, this);
}

void TopBarComponent::exportPresetDialog()
{
    // Drop any finished chooser (safe: we are never inside its callback here).
    presetFileChooser.reset();

    auto dir = util::UserData::getPresetDirectory();
    dir.createDirectory(); // best effort; the chooser can still pick elsewhere

    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Export Preset", dir.getChildFile ("My Preset.nvpreset"), "*.nvpreset", true, false, this);

    const juce::Component::SafePointer<TopBarComponent> safeThis (this);
    presetFileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();
            if (file == juce::File())
                return;

            juce::String error;
            if (! util::PresetIO::save (safeThis->processor.apvts, file,
                                        util::PresetIO::nameFromFile (file), error))
                showPresetError ("Export Preset", error, safeThis.getComponent());
        });
}

void TopBarComponent::importPresetDialog()
{
    presetFileChooser.reset();

    const auto dir = util::UserData::getPresetDirectory();
    const auto start = dir.isDirectory() ? dir : juce::File();

    // Accept *.nvpreset and raw session-state XML (PresetIO accepts both).
    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Import Preset", start, "*.nvpreset;*.xml", true, false, this);

    const juce::Component::SafePointer<TopBarComponent> safeThis (this);
    presetFileChooser->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();
            if (file == juce::File())
                return;

            juce::String error;
            if (! util::PresetIO::load (safeThis->processor, file, error))
                showPresetError ("Import Preset", error, safeThis.getComponent());
        });
}

void TopBarComponent::openPresetFolder()
{
    const auto dir = util::UserData::getPresetDirectory();

    if (const auto r = dir.createDirectory(); r.failed())
    {
        showPresetError ("Show Preset Folder",
                         "Cannot create the preset folder:\n" + r.getErrorMessage(), this);
        return;
    }

    dir.revealToUser();
}

void TopBarComponent::showPresetError (const juce::String& title, const juce::String& message,
                                       juce::Component* parent)
{
    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                 title, message, parent);
}

//==============================================================================
void TopBarComponent::showSettings()
{
    // Only one settings window at a time: focus the existing one if open.
    if (settingsContent != nullptr)
    {
        if (auto* window = settingsContent->findParentComponentOfClass<juce::DialogWindow>())
            window->toFront (true);
        return;
    }

    const float currentScale = getUserScale != nullptr ? getUserScale() : 1.0f;
    const juce::Component::SafePointer<TopBarComponent> safeThis (this);
    auto* dialog = new SettingsDialog (
        processor.apvts, currentScale,
        [safeThis] (float scale)
        {
            if (safeThis != nullptr && safeThis->setUserScale != nullptr)
                safeThis->setUserScale (scale);
        },
        [safeThis]
        {
            return safeThis != nullptr ? safeThis->processor.getReportedLatencySamples() : 0;
        },
        processor.getSampleRate());
    // T72: the dialog sizes itself from the current UI scale.

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (dialog);
    options.dialogTitle = "Settings";
    options.dialogBackgroundColour = dopamine::surface (dopamine::Surface::canvas);
    options.escapeKeyTriggersCloseButton = true;
    // T39: native title bar — light (the JUCE custom bar was dark and hid the
    // close button on the light theme). Native title adds its own height.
    options.useNativeTitleBar = true;
    options.resizable = false;
    settingsContent = dialog; // SafePointer: nulls itself when the window closes
    options.launchAsync();
}
} // namespace ui
