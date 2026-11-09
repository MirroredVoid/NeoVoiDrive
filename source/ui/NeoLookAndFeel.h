#pragma once

/*
    NeoLookAndFeel (T15, restyled T64): neumorphism + dopamine accents.

    Surfaces and controls share one bright neutral blue-grey family; volume is
    modelled with bidirectional soft shadows (white top-left, hue-darkened
    bottom-right) instead of borders, and recessed elements reverse the pair.
    Accents are semantic (see DopaminePalette.h) — the palette header is the
    only place that defines colour literals.

    All existing Tokens names survive as a facade so components and tests keep
    compiling; every value delegates to ui::dopamine. Radii stay on the 8 pt
    grid (cards 24, wells 16, buttons 16, small 8).

    Every colour pairing is verified for WCAG AA in NeoLookAndFeelTests.
*/

#include "ui/DopaminePalette.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace ui
{

class NeoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    /** Visual face state shared by the neumorphic helpers. */
    enum class FaceState { normal, hover, pressed, on, disabled };
    /** @param installAsDefault  Install this instance as the process-global
        default L&F (needed for font typeface resolution — JUCE resolves
        typefaces through the global default, not the component tree).
        Only the main editor's theme should pass true; popups (e.g. the
        Settings dialog theme) pass false so closing them cannot strip the
        font from the still-open editor. */
    explicit NeoLookAndFeel (bool installAsDefault = true);
    ~NeoLookAndFeel() override;

    /** Global font: Google Sans Code variable font (MONO,wght axes), EMBEDDED
        as binary data via juce_add_binary_data (NeoVoiDriveData target).
        MONO pinned to 1.0 (full monospace); regular = wght 500, bold = 700.
        Falls back to the default sans-serif if the typeface data is
        unusable (never crashes). */
    static juce::Typeface::Ptr getGlobalTypeface (bool bold) noexcept;

    /** The wght-600 ("medium") instance — for emphasis text (buttons, tabs,
        numeric readouts) where the bold/regular binary is too coarse
        (UI_STYLE_GUIDE §7). */
    static juce::Typeface::Ptr getMediumTypeface() noexcept;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override;

    /** Popup menus are separate top-level windows and therefore do not inherit
        the editor's AffineTransform. Scale their font, border, item height and
        drawing metrics explicitly so they match the rest of the UI when the
        user changes "UI Size" (75–150 %). */
    void setUiScale (float scale) noexcept;
    float getUiScale() const noexcept { return uiScale; }

    juce::Font getPopupMenuFont() override;
    // §5.10: insets the item area so items clear the self-drawn soft shadow.
    int getPopupMenuBorderSize() override;
    // Combo popups derive their item height from the (transformed) label
    // height, so scale it into physical pixels for the popup window.
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&,
                                                             juce::Label&) override;

    //==========================================================================
    // Design tokens — public so tests verify contrast / grid invariants.
    //==========================================================================
    struct Tokens
    {
        // ---- 8 pt grid ----
        static constexpr float gridUnit       = 8.0f;
        static constexpr float radiusSmall    = 8.0f;
        static constexpr float radiusMedium   = 24.0f;
        static constexpr float radiusLarge    = 24.0f;
        static constexpr float controlHeight  = 32.0f;
        static constexpr float controlHeightLarge = 40.0f;

        // ---- radii additions (neumorphic: large cards, capsule controls) ----
        static constexpr float radiusButton   = 16.0f;
        static constexpr float radiusTab      = 24.0f;
        static constexpr float radiusWell     = 16.0f;

        // ---- palette facade (single source: ui::dopamine) ----
        static juce::Colour background()  { return dopamine::surface (dopamine::Surface::canvas); }
        static juce::Colour surface()     { return dopamine::surface (dopamine::Surface::surface); }
        static juce::Colour well()        { return dopamine::surface (dopamine::Surface::well); }
        static juce::Colour wellDeep()    { return dopamine::surface (dopamine::Surface::wellDeep); }
        static juce::Colour line()        { return dopamine::surface (dopamine::Surface::line); }
        static juce::Colour ink()         { return dopamine::surface (dopamine::Surface::ink); }
        static juce::Colour inkSoft()     { return dopamine::surface (dopamine::Surface::inkSoft); }
        static juce::Colour inkFaint()    { return dopamine::surface (dopamine::Surface::inkFaint); }
        static juce::Colour onAccent()    { return dopamine::surface (dopamine::Surface::onAccent); }
        static juce::Colour accent()      { return dopamine::accent (dopamine::Role::global).base; }
        static juce::Colour accentHover() { return dopamine::accent (dopamine::Role::global).hover; }
        static juce::Colour accentActive(){ return dopamine::accent (dopamine::Role::global).active; }
        static juce::Colour surfaceAlt()  { return dopamine::surface (dopamine::Surface::surfaceAlt); }
        static juce::Colour hover()       { return dopamine::surface (dopamine::Surface::surfaceAlt); }

        static juce::Colour accentFor (dopamine::Role r) { return dopamine::accent (r).base; }
        static juce::Colour bandAccent (int band0)       { return dopamine::bandAccent (band0); }
        static juce::Colour sectionAccent (dopamine::Section s) { return dopamine::sectionAccent (s); }

        // ---- meter segment colours ----
        static juce::Colour meterOk()     { return dopamine::meterOk(); }
        static juce::Colour meterWarn()   { return dopamine::meterWarn(); }
        static juce::Colour meterHot()    { return dopamine::meterHot(); }

        static juce::Colour panel()        { return dopamine::surface (dopamine::Surface::panel); }
        static juce::Colour shadow()       { return dopamine::shadowDark (dopamine::Section::global); }
        static juce::Colour highlight()    { return dopamine::shadowLight(); }

        // Disabled set (derived); disabled elements drop their shadow.
        static juce::Colour disabledFace()  { return dopamine::disabledFace(); }
        static juce::Colour disabledGlyph() { return dopamine::disabledGlyph(); }
        static juce::Colour disabledText()  { return dopamine::disabledText(); }
    };

    //==========================================================================
    // WCAG AA contrast math (single implementation in the palette).
    //==========================================================================
    static float relativeLuminance (juce::Colour c) noexcept
    {
        return dopamine::relativeLuminance (c);
    }

    static float contrastRatio (juce::Colour a, juce::Colour b) noexcept
    {
        return dopamine::contrastRatio (a, b);
    }

    //==========================================================================
    // Knob value formatting (user spec): at most 3 significant digits while
    // the value is < 1000 (0-2 decimals), full digits at >= 1000; the caller
    // appends the unit via setTextValueSuffix.
    //==========================================================================
    static juce::String formatKnobValue (double value) noexcept
    {
        const auto av = std::abs (value);
        if (av >= 1000.0)
            return juce::String (juce::roundToInt (value)); // full digits (String(v,0) keeps %g)

        const int intDigits = av < 1.0 ? 1
                                       : static_cast<int> (std::floor (std::log10 (av))) + 1;
        const int decimals = juce::jlimit (0, 2, 3 - intDigits);

        // JUCE String(v, 0) keeps the %g default (999.99 stays "999.99") —
        // round explicitly when no decimals are wanted.
        if (decimals == 0)
            return juce::String (juce::roundToInt (value));

        auto s = juce::String (value, decimals);
        if (s.containsChar ('.'))
        {
            while (s.getLastCharacter() == '0')
                s = s.dropLastCharacters (1);
            if (s.getLastCharacter() == '.')
                s = s.dropLastCharacters (1);
        }
        return s;
    }

    /** Unit suffix for a parameter ID (knob value displays). */
    static juce::String unitForParam (const juce::String& id) noexcept
    {
        // T32: sideband units FIRST — the generic `_depth`/`_rate` rules below
        // would otherwise catch sideband_depth (" ct") etc.
        if (id.endsWith ("sideband_depth"))     return " %";
        if (id.endsWith ("sideband_fm_ratio"))  return {};
        if (id.endsWith ("sideband_roughness")) return " %";
        if (id.endsWith ("sideband_lowcut"))    return " Hz";
        if (id == "formant_shift")            return " st";
        if (id == "formant_quefrency")        return " ms";
        if (id == "dry_wet")                  return " %";
        if (id == "output_trim")              return " dB";
        // T66: exact guards before the generic _rate/_tone rules.
        if (id == "growl_amount")             return " %";
        if (id == "growl_rate")
            return juce::String::charToString (static_cast<juce::juce_wchar> (0x00D7)); // ×, ASCII-safe source
        if (id == "growl_tone")               return {};
        if (id.endsWith ("shimmer_depth"))    return " %";
        if (id.endsWith ("_depth"))           return " ct";
        if (id.endsWith ("_drive"))           return " %";
        if (id.endsWith ("_rate"))            return " Hz";
        if (id.endsWith ("_tone"))            return " Hz";
        if (id.endsWith ("_freq"))            return " Hz";   // xover1/2_freq
        if (id.endsWith ("_gain"))            return " dB";   // b{n}_gain (T37)
        if (id.endsWith ("_fog"))             return " %";    // b{n}_fog (T49)
        return {};
    }

    //==========================================================================
    // Neumorphic face helpers (T64). Section colours come from DopaminePalette;
    // components set a Section/Role property and the L&F resolves it by
    // walking the parent chain, so the component tree stays unchanged.
    //==========================================================================

    /** Blur radius scaled by the face size: small controls get small shadows. */
    static float neoBlur (juce::Rectangle<float> face) noexcept;

    /** Bidirectional soft shadow pair: white top-left + hue-darkened
        bottom-right. Both are cached blits (no per-frame DropShadow). */
    static void drawNeoShadowPair (juce::Graphics&, juce::Rectangle<float> face, float radius,
                                   dopamine::Section, bool ellipse = false, float alphaScale = 1.0f);

    /** Raised face: same-family gradient + light/dark shadow pair, no border.
        `pressed` flips to an inset well; `on` paints the section accent face. */
    static void drawNeoRaised (juce::Graphics&, juce::Rectangle<float> face, float radius,
                               dopamine::Section, FaceState, bool withShadow = true);

    /** Flat panel face (large containers): NO convex gradient and no top
        highlight — only a flat section-tinted fill plus the optional shadow. */
    static void drawNeoPanel (juce::Graphics&, juce::Rectangle<float> face, float radius,
                              dopamine::Section, bool withShadow = true);

    /** Inset well: darker same-family fill + reversed inner shadows (top/left
        dark, bottom/right light). No 1 px outline. */
    static void drawNeoInset (juce::Graphics&, juce::Rectangle<float> face, float radius,
                              dopamine::Section, bool shadingOnly = false);

    /** Convex ellipse (knob body, thumbs, switch knob, radio dot). `tint`
        blends the semantic accent into the face (knobs keep their role colour
        readable even at 0). */
    static void drawNeoConvexEllipse (juce::Graphics&, juce::Rectangle<float> face,
                                      dopamine::Section, FaceState,
                                      juce::Colour tint = dopamine::transparent());

    //==========================================================================
    // Legacy helper names kept as thin wrappers (components/tests still call
    // them). New code should use the section-aware overloads above.
    //==========================================================================

    /** Raised face wrapper (global section). */
    static void drawRaisedFace (juce::Graphics&, juce::Rectangle<float> face, float radius,
                                bool highlighted = false, bool pressed = false);

    /** Accent face wrapper (global accent). */
    static void drawAccentFace (juce::Graphics&, juce::Rectangle<float> face, float radius,
                                bool pressed = false);

    /** Inset well wrapper (global section). */
    static void drawInsetWell (juce::Graphics&, juce::Rectangle<float> face, float radius);

    /** Inset shading only (global section) — for wells whose content is drawn
        between the fill and the shading (spectrum well). */
    static void drawInsetWellShading (juce::Graphics&, juce::Rectangle<float> face, float radius);

    /** Convex ellipse wrapper (global section). */
    static void drawConvexEllipse (juce::Graphics&, juce::Rectangle<float> face, bool enabled = true);

    /** Contact shadow for small controls: a short soft gradient band just
        under the face, fully inside the component bounds. */
    static void drawContactShadow (juce::Graphics&, juce::Rectangle<float> face, float radius,
                                   float alpha, float drop = 1.5f);

    /** Soft drop shadow under a rounded-rect face, pre-rendered into a cached
        image (per-frame DropShadowEffect is banned). The cached image is
        inflated by `blur` px around the face: the caller needs that much room
        inside its own component bounds (or draws from the parent). */
    static void drawSoftShadow (juce::Graphics&, juce::Rectangle<float> face, float radius,
                                float alpha = 0.14f, int blur = 12, int dy = 3);

    /** Same for an elliptical face (knob bodies, thumbs, handles). */
    static void drawSoftShadowEllipse (juce::Graphics&, juce::Rectangle<float> face,
                                       float alpha = 0.20f, int blur = 4, int dy = 2);

    //==========================================================================
    // Drawing (V4 overrides).
    //==========================================================================
    /** T38: drop the popup window's drop-shadow flag — the system shadow is
        a RECTANGLE outline, which stacked under the rounded menu as the
        reported "rectangle + rounded rect" (LookAndFeel_V2 defaults to
        windowHasDropShadow). */
    int getMenuWindowFlags() override { return 0; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    int getSliderThumbRadius (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    // §7: button text uses the wght-600 emphasis face at 14 px.
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool isEnabled,
                      bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawLevelMeter (juce::Graphics&, int width, int height, float level) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    // UI_STYLE_GUIDE §5.11: band tabs (previously the JUCE default drawing —
    // the most visible style break).
    void drawTabButton (juce::TabBarButton&, juce::Graphics&,
                        bool isMouseOver, bool isMouseDown) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&,
                                       int w, int h) override;
    /** Tabs always fill the strip: equal shares, last tab takes the remainder. */
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    /** No overlap/gap between the full-width tab segments. */
    int getTabButtonOverlap (int tabDepth) override;
    int getTabButtonSpaceAroundImage() override;

    /** Visible knob disc inside a slider's bounds. Single source of truth for
        drawKnobFace and layout code that aligns other controls to a knob edge. */
    static juce::Rectangle<float> getKnobDiscBounds (const juce::Rectangle<float>& sliderBounds) noexcept;

    /** Visible knob disc in the slider's parent coordinates (accounts for the
        text-box layout that JUCE removes before drawRotarySlider). */
    static juce::Rectangle<float> getKnobDiscBounds (juce::Slider& slider);

private:
    const bool installAsDefault = true;
    float uiScale = 1.0f;

    // Linked list of installAsDefault instances (multi-editor safety: when
    // the current global default dies, the default is handed to another
    // live theme instead of being reset to JUCE's built-in — review).
    NeoLookAndFeel* nextThemeInstance = nullptr;

    /** Same 14 px emphasis face as buttons/tabs (JUCE default grows to 16 px
        and outranked section titles). */
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    static void drawKnobFace (juce::Graphics&, const juce::Rectangle<float>& bounds,
                              float position, float startAngle, float endAngle,
                              bool enabled, bool highlighted, bool dragging,
                              dopamine::Section, dopamine::Role) noexcept;
};

} // namespace ui
