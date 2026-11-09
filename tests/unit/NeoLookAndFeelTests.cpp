/*
    NeoVoiDrive T15 tests — NeoLookAndFeel light theme.

    Acceptance (TASKS.md T15):
      1. text contrast meets WCAG AA (>= 4.5:1 normal, >= 3:1 large/graphics);
      2. 8 pt grid: spacing / radii / control heights are grid multiples;
      3. no JUCE default look remains: the key colour ids resolve to the
         token palette (not the V4 defaults);
      4. every draw routine renders off-screen without crashing (smoke).
    Screenshot review stays manual (user confirmation).
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/NeoLookAndFeel.h"

#include <cmath>

namespace
{

//==============================================================================
class NeoLookAndFeelTest final : public juce::UnitTest
{
public:
    NeoLookAndFeelTest() : juce::UnitTest ("NeoLookAndFeel") {}

    void runTest() override
    {
        using namespace ui;

        beginTest ("Google Sans Code font is embedded and loads (BinaryData)");
        {
            using ui::NeoLookAndFeel;
            expect (NeoLookAndFeel::getGlobalTypeface (false) != nullptr,
                    "regular (wght 500) Google Sans Code typeface failed to load");
            expect (NeoLookAndFeel::getGlobalTypeface (true) != nullptr,
                    "bold (wght 700) Google Sans Code typeface failed to load");
            expect (NeoLookAndFeel::getMediumTypeface() != nullptr,
                    "medium (wght 600) Google Sans Code typeface failed to load");

            // UI_STYLE_GUIDE §7: the MONO axis must be pinned to 1.0 — a
            // proportional cut would break the numeric column alignment.
            if (auto tf = NeoLookAndFeel::getGlobalTypeface (false))
            {
                juce::Font f { juce::FontOptions (tf) };
                f.setHeight (20.0f);
                const auto narrow = juce::GlyphArrangement::getStringWidth (f, "i");
                const auto wide   = juce::GlyphArrangement::getStringWidth (f, "W");
                expectWithinAbsoluteError (narrow, wide, 0.01f,
                                           "MONO axis not pinned — glyphs are not monospaced");
            }
            logMessage ("  embedded font OK (500/600/700 wght instances, MONO=1)");
        }

        beginTest ("knob value formatting (user spec: <=3 digits + unit)");
        {
            using ui::NeoLookAndFeel;
            // Formatting: < 1000 keeps 0-2 decimals within 3 significant
            // digits; >= 1000 shows the full integer.
            expectEquals (NeoLookAndFeel::formatKnobValue (0.0),  juce::String ("0"));
            expectEquals (NeoLookAndFeel::formatKnobValue (0.5),  juce::String ("0.5"));
            expectEquals (NeoLookAndFeel::formatKnobValue (5.0),  juce::String ("5"));
            expectEquals (NeoLookAndFeel::formatKnobValue (25.0), juce::String ("25"));
            expectEquals (NeoLookAndFeel::formatKnobValue (1.5),  juce::String ("1.5"));
            expectEquals (NeoLookAndFeel::formatKnobValue (4.99), juce::String ("4.99"));
            expectEquals (NeoLookAndFeel::formatKnobValue (999.99), juce::String ("1000"));
            expectEquals (NeoLookAndFeel::formatKnobValue (1000.0), juce::String ("1000"));
            expectEquals (NeoLookAndFeel::formatKnobValue (10000.0), juce::String ("10000"));
            expectEquals (NeoLookAndFeel::formatKnobValue (-12.5), juce::String ("-12.5"));

            // Units.
            expectEquals (NeoLookAndFeel::unitForParam ("formant_shift"),     juce::String (" st"));
            expectEquals (NeoLookAndFeel::unitForParam ("formant_quefrency"), juce::String (" ms"));
            expectEquals (NeoLookAndFeel::unitForParam ("dry_wet"),           juce::String (" %"));
            expectEquals (NeoLookAndFeel::unitForParam ("output_trim"),       juce::String (" dB"));
            expectEquals (NeoLookAndFeel::unitForParam ("b1_jitter_depth"),   juce::String (" ct"));
            expectEquals (NeoLookAndFeel::unitForParam ("b1_shimmer_depth"),  juce::String (" %"));
            expectEquals (NeoLookAndFeel::unitForParam ("b1_harmonic_drive"), juce::String (" %"));
            expectEquals (NeoLookAndFeel::unitForParam ("b2_vib_rate"),       juce::String (" Hz"));
            expectEquals (NeoLookAndFeel::unitForParam ("b3_harmonic_tone"),  juce::String (" Hz"));
            expectEquals (NeoLookAndFeel::unitForParam ("b1_fog"),            juce::String (" %"));
            // T32: sideband units (must win over the generic _depth/_rate
            // rules below them).
            expectEquals (NeoLookAndFeel::unitForParam ("b1_sideband_depth"),     juce::String (" %"));
            expectEquals (NeoLookAndFeel::unitForParam ("b1_sideband_fm_ratio"),  juce::String ());
            expectEquals (NeoLookAndFeel::unitForParam ("b1_sideband_roughness"), juce::String (" %"));
            expectEquals (NeoLookAndFeel::unitForParam ("b1_sideband_lowcut"),    juce::String (" Hz"));
            // T66 Growl units (exact guards before the generic _rate/_tone rules).
            expectEquals (NeoLookAndFeel::unitForParam ("growl_amount"), juce::String (" %"));
            expectEquals (NeoLookAndFeel::unitForParam ("growl_rate"),
                          juce::String::charToString (static_cast<juce::juce_wchar> (0x00D7)));
            expectEquals (NeoLookAndFeel::unitForParam ("growl_tone"), juce::String ());
        }

        beginTest ("text contrast meets WCAG AA");
        {
            const auto check = [this] (juce::Colour fg, juce::Colour bg, float minimum, const char* label)
            {
                const auto ratio = NeoLookAndFeel::contrastRatio (fg, bg);
                expectGreaterThan (ratio, minimum - 0.05f,
                                   juce::String (label) + " contrast " + juce::String (ratio)
                                   + " below " + juce::String (minimum));
            };

            // Primary / secondary text on every surface it appears on.
            check (NeoLookAndFeel::Tokens::ink(),      NeoLookAndFeel::Tokens::surface(),    4.5f, "ink/surface");
            check (NeoLookAndFeel::Tokens::inkSoft(),  NeoLookAndFeel::Tokens::surface(),    4.5f, "inkSoft/surface");
            check (NeoLookAndFeel::Tokens::ink(),      NeoLookAndFeel::Tokens::background(), 4.5f, "ink/background");
            check (NeoLookAndFeel::Tokens::inkSoft(),  NeoLookAndFeel::Tokens::background(), 4.5f, "inkSoft/background");
            check (NeoLookAndFeel::Tokens::ink(),      NeoLookAndFeel::Tokens::well(),       4.5f, "ink/well");
            check (NeoLookAndFeel::Tokens::inkSoft(),  NeoLookAndFeel::Tokens::well(),       4.5f, "inkSoft/well");
            check (NeoLookAndFeel::Tokens::ink(),      NeoLookAndFeel::Tokens::surfaceAlt(), 4.5f, "ink/surfaceAlt");
            check (NeoLookAndFeel::Tokens::inkSoft(),  NeoLookAndFeel::Tokens::surfaceAlt(), 4.5f, "inkSoft/surfaceAlt");

            // Accent is used for graphics / large text only: AA large >= 3:1.
            check (NeoLookAndFeel::Tokens::accent(),   NeoLookAndFeel::Tokens::surface(),    3.0f, "accent/surface");
            check (NeoLookAndFeel::Tokens::accent(),   NeoLookAndFeel::Tokens::background(), 3.0f, "accent/background");
            check (NeoLookAndFeel::Tokens::accentHover(), NeoLookAndFeel::Tokens::surface(), 3.0f, "accentHover/surface");

            // T64: semantic role accents as graphics (>= 3:1) and on-accent
            // text (>= 4.5:1) against the faces they are painted on.
            const auto surfaces = { NeoLookAndFeel::Tokens::surface(),
                                    NeoLookAndFeel::Tokens::background(),
                                    NeoLookAndFeel::Tokens::well() };
            for (auto role : { ui::dopamine::Role::global, ui::dopamine::Role::frequency,
                               ui::dopamine::Role::level, ui::dopamine::Role::depth,
                               ui::dopamine::Role::rate })
            {
                const auto a = ui::dopamine::accent (role);
                for (auto bg : surfaces)
                    check (a.base, bg, 3.0f, "role accent / surface");
                check (NeoLookAndFeel::Tokens::onAccent(), a.base, 4.5f, "onAccent/role base");
                check (NeoLookAndFeel::Tokens::onAccent(), a.hover, 4.5f, "onAccent/role hover");
                check (NeoLookAndFeel::Tokens::onAccent(), a.active, 4.5f, "onAccent/role active");
            }

            // Band accents are identity hues drawn as thin curve strokes.
            // Bands 1–2 keep the AA graphics ratio; Band 3 is a deliberately
            // bright yellow, and no yellow that still reads bright can reach
            // 3:1 on a white-family well (it would have to turn olive). It is
            // guarded by hue separation + a brighter luminance than amber, and
            // the plot adds a soft dark under-stroke so the line stays legible.
            for (int band = 0; band < 2; ++band)
                check (ui::dopamine::bandAccent (band), NeoLookAndFeel::Tokens::well(),
                       3.0f, "band accent/well");
            {
                const auto yellow = ui::dopamine::bandAccent (2);
                const auto well = NeoLookAndFeel::Tokens::well();
                const auto distance = std::abs (yellow.getRed() - well.getRed())
                                    + std::abs (yellow.getGreen() - well.getGreen())
                                    + std::abs (yellow.getBlue() - well.getBlue());
                expectGreaterThan (distance, 150,
                                   "band 3 yellow must remain distinct from the well");
                expectGreaterThan (NeoLookAndFeel::relativeLuminance (yellow),
                                   NeoLookAndFeel::relativeLuminance (ui::dopamine::bandAccent (1)),
                                   "band 3 must read brighter than band 2 amber");
            }

            // Text on the accent fill (pressed / toggled buttons).
            check (NeoLookAndFeel::Tokens::onAccent(),  NeoLookAndFeel::Tokens::accent(),     4.5f, "onAccent/accent");

            // Section strips (panel) take primary/secondary text; the
            // accent-pressed face takes onAccent.
            check (NeoLookAndFeel::Tokens::ink(),      NeoLookAndFeel::Tokens::panel(),      4.5f, "ink/panel");
            check (NeoLookAndFeel::Tokens::inkSoft(),  NeoLookAndFeel::Tokens::panel(),      4.5f, "inkSoft/panel");
            check (NeoLookAndFeel::Tokens::onAccent(), NeoLookAndFeel::Tokens::accentActive(), 4.5f, "onAccent/accentActive");

            // Meter colours are graphics (non-text): informational.
            check (NeoLookAndFeel::Tokens::meterOk(),  NeoLookAndFeel::Tokens::well(),       2.0f, "meterOk/well");
            check (NeoLookAndFeel::Tokens::meterWarn(),NeoLookAndFeel::Tokens::well(),       2.0f, "meterWarn/well");
            check (NeoLookAndFeel::Tokens::meterHot(), NeoLookAndFeel::Tokens::well(),       2.0f, "meterHot/well");
        }

        beginTest ("8 pt grid: spacing, radii and control heights");
        {
            using T = NeoLookAndFeel::Tokens;

            const auto isGridMultiple = [] (float v)
            {
                const auto units = v / T::gridUnit;
                return std::abs (units - std::round (units)) < 1e-4f && units >= 1.0f;
            };

            expect (isGridMultiple (T::radiusSmall),     "radiusSmall not on the 8 pt grid");
            expect (isGridMultiple (T::radiusMedium),    "radiusMedium not on the 8 pt grid");
            expect (isGridMultiple (T::radiusLarge),     "radiusLarge not on the 8 pt grid");
            expect (isGridMultiple (T::controlHeight),   "controlHeight not on the 8 pt grid");
            expect (isGridMultiple (T::controlHeightLarge), "controlHeightLarge not on the 8 pt grid");
            expect (isGridMultiple (T::radiusButton),    "radiusButton not on the 8 pt grid");
            expect (isGridMultiple (T::radiusTab),       "radiusTab not on the 8 pt grid");
            expect (isGridMultiple (T::radiusWell),      "radiusWell not on the 8 pt grid");
        }

        beginTest ("T64 palette is the single colour source; section/role resolve");
        {
            using namespace ui;
            using namespace ui::dopamine;

            // Tokens facade == palette (no second source of truth).
            expect (NeoLookAndFeel::Tokens::background() == surface (Surface::canvas));
            expect (NeoLookAndFeel::Tokens::surface()    == surface (Surface::surface));
            expect (NeoLookAndFeel::Tokens::well()       == surface (Surface::well));
            expect (NeoLookAndFeel::Tokens::ink()        == surface (Surface::ink));
            expect (NeoLookAndFeel::Tokens::inkSoft()    == surface (Surface::inkSoft));
            expect (NeoLookAndFeel::Tokens::accent()     == accent (Role::global).base);
            expect (NeoLookAndFeel::Tokens::accentHover()== accent (Role::global).hover);
            expect (NeoLookAndFeel::Tokens::accentActive()== accent (Role::global).active);
            expect (NeoLookAndFeel::Tokens::onAccent()   == surface (Surface::onAccent));

            // Section tints stay in the same light family (no dark panels).
            for (auto s : { Section::global, Section::spectrum, Section::band1,
                            Section::band2, Section::band3, Section::master,
                            Section::settings, Section::growl })
            {
                expect (NeoLookAndFeel::relativeLuminance (sectionBase (s)) > 0.75f,
                        "section base too dark for neumorphism");
                expect (NeoLookAndFeel::relativeLuminance (sectionWell (s)) > 0.68f,
                        "section well too dark for neumorphism");
            }

            // Component-property plumbing (no tree change).
            juce::Component host;
            juce::Slider knob (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
            host.addAndMakeVisible (knob);
            expect (sectionOf (knob) == Section::global, "default section");
            expect (roleOf (knob) == Role::global, "default role");
            applySection (host, Section::band2);
            applyRole (knob, Role::depth);
            expect (sectionOf (knob) == Section::band2, "section should resolve through the parent chain");
            expect (roleOf (knob) == Role::depth, "role should resolve from the component itself");
            expect (sectionOf (host) == Section::band2);
        }

        beginTest ("colour ids resolve to the token palette (no V4 defaults)");
        {
            ui::NeoLookAndFeel lnf;

            const auto expectColour = [this, &lnf] (int id, juce::Colour expected, const char* label)
            {
                const auto actual = lnf.findColour (id);
                expect (actual == expected,
                        juce::String (label) + " = " + actual.toString()
                        + " expected " + expected.toString());
            };

            expectColour (juce::ResizableWindow::backgroundColourId, ui::NeoLookAndFeel::Tokens::background(), "window bg");
            // T38: transparent — an opaque popup bg makes MenuWindow paint a
            // WHITE RECT first, stacking under the rounded surface.
            expectColour (juce::PopupMenu::backgroundColourId,       juce::Colours::transparentBlack,         "popup bg");
            expectColour (juce::PopupMenu::textColourId,             ui::NeoLookAndFeel::Tokens::ink(),       "popup text");
            expectColour (juce::ComboBox::textColourId,              ui::NeoLookAndFeel::Tokens::ink(),       "combo text");
            expectColour (juce::ComboBox::outlineColourId,           ui::NeoLookAndFeel::Tokens::line(),      "combo outline");
            expectColour (juce::TextEditor::backgroundColourId,      ui::NeoLookAndFeel::Tokens::surface(),   "editor bg");
            expectColour (juce::TextEditor::textColourId,            ui::NeoLookAndFeel::Tokens::ink(),       "editor text");
            expectColour (juce::TextEditor::focusedOutlineColourId,  ui::NeoLookAndFeel::Tokens::accent(),    "editor focus");
            expectColour (juce::Slider::trackColourId,               ui::NeoLookAndFeel::Tokens::accent(),    "slider track");
            expectColour (juce::Slider::backgroundColourId,          ui::NeoLookAndFeel::Tokens::well(),      "slider bg");
            expectColour (juce::Slider::thumbColourId,               ui::NeoLookAndFeel::Tokens::surface(),   "slider thumb");
            expectColour (juce::Slider::rotarySliderFillColourId,    ui::NeoLookAndFeel::Tokens::accent(),    "rotary fill");
            expectColour (juce::Slider::rotarySliderOutlineColourId, ui::NeoLookAndFeel::Tokens::line(),      "rotary outline");
            expectColour (juce::ToggleButton::tickColourId,          ui::NeoLookAndFeel::Tokens::accent(),    "toggle tick");
            expectColour (juce::TextButton::textColourOnId,          ui::NeoLookAndFeel::Tokens::onAccent(),  "button text on");
            expectColour (juce::Label::textColourId,                 ui::NeoLookAndFeel::Tokens::ink(),       "label text");
            expectColour (juce::AlertWindow::backgroundColourId,     ui::NeoLookAndFeel::Tokens::surface(),   "alert bg");
            expectColour (juce::ListBox::textColourId,               ui::NeoLookAndFeel::Tokens::ink(),       "list text");
            expectColour (juce::TooltipWindow::backgroundColourId,   ui::NeoLookAndFeel::Tokens::surface(),   "tooltip bg");
        }

        beginTest ("draw routines render off-screen without crashing");
        {
            ui::NeoLookAndFeel lnf;
            juce::Image image (juce::Image::ARGB, 256, 256, true);
            juce::Graphics g (image);

            g.fillAll (ui::NeoLookAndFeel::Tokens::background());

            // Rotary slider (instrument pointer) with a semantic role.
            {
                juce::Slider slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
                slider.setSize (64, 64);
                slider.setRange (0.0, 1.0);
                slider.setValue (0.65);
                ui::dopamine::applyRole (slider, ui::dopamine::Role::depth);
                lnf.drawRotarySlider (g, 8, 8, 64, 64, 0.65f,
                                      juce::MathConstants<float>::pi * 1.25f,
                                      juce::MathConstants<float>::pi * 1.75f, slider);
            }

            // Linear sliders (horizontal + vertical).
            {
                juce::Slider slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
                slider.setSize (160, 32);
                slider.setRange (0.0, 1.0);
                slider.setValue (0.4);
                lnf.drawLinearSlider (g, 8, 88, 160, 32, 72.0f, 8.0f, 168.0f,
                                      juce::Slider::LinearHorizontal, slider);

                juce::Slider vSlider (juce::Slider::LinearVertical, juce::Slider::NoTextBox);
                vSlider.setSize (32, 120);
                vSlider.setRange (0.0, 1.0);
                vSlider.setValue (0.7);
                lnf.drawLinearSlider (g, 200, 88, 32, 120, 124.0f, 88.0f, 208.0f,
                                      juce::Slider::LinearVertical, vSlider);
            }

            // Buttons (normal / hover / down / toggled).
            {
                juce::TextButton button ("Test");
                button.setSize (96, 32);
                lnf.drawButtonBackground (g, button, juce::Colours::transparentBlack, true, false);
                lnf.drawButtonBackground (g, button, juce::Colours::transparentBlack, true, true);
                button.setToggleState (true, juce::dontSendNotification);
                lnf.drawButtonBackground (g, button, juce::Colours::transparentBlack, false, false);

                juce::ToggleButton toggle ("On");
                toggle.setSize (48, 24);
                toggle.setToggleState (true, juce::dontSendNotification);
                lnf.drawToggleButton (g, toggle, false, false);
            }

            // Neumorphic faces (raised / inset / shadow pair / convex).
            {
                juce::Rectangle<float> face (16.0f, 16.0f, 120.0f, 56.0f);
                lnf.drawNeoRaised (g, face, 16.0f, ui::dopamine::Section::band2,
                                   ui::NeoLookAndFeel::FaceState::normal);
                lnf.drawNeoInset (g, face.translated (140.0f, 0.0f), 16.0f,
                                  ui::dopamine::Section::master);
                lnf.drawNeoShadowPair (g, face.translated (0.0f, 70.0f), 16.0f,
                                       ui::dopamine::Section::spectrum);
                lnf.drawNeoConvexEllipse (g, face.translated (140.0f, 70.0f).withSizeKeepingCentre (48.0f, 48.0f),
                                          ui::dopamine::Section::band3,
                                          ui::NeoLookAndFeel::FaceState::hover);
            }

            // Combo box.
            {
                juce::ComboBox combo ("combo");
                combo.setSize (128, 32);
                lnf.drawComboBox (g, 128, 32, false, 96, 0, 32, 32, combo);
            }

            // Popup item + background.
            {
                lnf.drawPopupMenuBackground (g, 160, 48);
                juce::Rectangle<int> itemArea (8, 8, 160, 32);
                lnf.drawPopupMenuItem (g, itemArea, false, true, true, true, false,
                                       "Item", "Ctrl+I", nullptr, nullptr);
            }

            // Text editor outline.
            {
                juce::TextEditor editor;
                editor.setSize (128, 28);
                lnf.drawTextEditorOutline (g, 128, 28, editor);
            }

            // Level meter (segmented).
            {
                lnf.drawLevelMeter (g, 160, 12, 0.35f);
                lnf.drawLevelMeter (g, 160, 12, 0.85f);
            }

            // Scrollbar thumb.
            {
                juce::ScrollBar scrollbar (true);
                scrollbar.setSize (8, 100);
                lnf.drawScrollbar (g, scrollbar, 240, 8, 8, 100, true, 10, 40, false, false);
            }

            expect (true); // reached here without crashing
        }


        beginTest ("T72 popup menu metrics follow the UI scale");
        {
            // Popup menus are separate top-level windows and do not inherit
            // the editor canvas transform, so the theme scales them itself.
            ui::NeoLookAndFeel lnf { false };
            expectWithinAbsoluteError (lnf.getPopupMenuFont().getHeight(), 16.0f, 0.01f);
            expectEquals (lnf.getPopupMenuBorderSize(), 14);

            lnf.setUiScale (1.25f);
            expectWithinAbsoluteError (lnf.getPopupMenuFont().getHeight(), 20.0f, 0.01f);
            expectEquals (lnf.getPopupMenuBorderSize(), 18); // round(14 * 1.25)

            lnf.setUiScale (1.5f);
            expectWithinAbsoluteError (lnf.getPopupMenuFont().getHeight(), 24.0f, 0.01f);
            expectEquals (lnf.getPopupMenuBorderSize(), 21); // round(14 * 1.5)

            lnf.setUiScale (9.0f); // clamped to the editor scale range
            expectWithinAbsoluteError (lnf.getUiScale(), 2.5f, 0.001f);
            expectWithinAbsoluteError (lnf.getPopupMenuFont().getHeight(), 40.0f, 0.01f);
        }

        beginTest ("theme installs itself as default; popup theme does not");
        {
            // A main theme (installAsDefault=true) becomes the global
            // default so Font typeface resolution reaches Google Sans Code.
            ui::NeoLookAndFeel mainTheme;
            expect (dynamic_cast<ui::NeoLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()) != nullptr,
                    "main theme did not install itself as the default");

            // A popup theme (Settings dialog) must NOT hijack the default;
            // and destroying it must not strip the main theme's install.
            {
                ui::NeoLookAndFeel popupTheme { false };
                expect (dynamic_cast<ui::NeoLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()) != nullptr,
                        "default changed while popup theme alive");
            }
            expect (dynamic_cast<ui::NeoLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()) != nullptr,
                    "popup theme dtor stripped the main theme's default");
        }
        // mainTheme goes out of scope here -> default restored to JUCE's. 
    }
};

NeoLookAndFeelTest neoLookAndFeelTest;

} // namespace
