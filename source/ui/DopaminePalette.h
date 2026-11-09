#pragma once

/*
    DopaminePalette (T64): the ONLY colour source for the NeoVoiDrive UI.

    Direction: neumorphism (same-family surfaces, bidirectional soft shadows)
    with a dopamine accent system. Accents are semantic:
      global      indigo  — selection / focus / menu ticks / Bypass ON
      frequency   teal    — crossover + formant frequency controls, cursor
      level       orange  — gains, dry/wet, output trim, meters
      depth       magenta — jitter / shimmer / vibrato depth, Rough
      rate        violet  — jitter / shimmer / vibrato rate
      band 1..3   coral / amber / bright yellow — plot envelopes + identity

    Surfaces are a bright neutral blue-grey family (no pure white fills);
    #FFFFFF survives only as the top-left light shadow token.

    All hex literals live here. No other UI file may hard-code a colour.
    WCAG checks live in NeoLookAndFeelTests (text >= 4.5:1, accents >= 3:1).
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace ui::dopamine
{

//==============================================================================
enum class Section
{
    global,
    spectrum,
    band1,
    band2,
    band3,
    master,
    settings,
    growl
};

enum class Role
{
    global,
    frequency,
    level,
    depth,
    rate
};

enum class Surface
{
    canvas,
    surface,
    surfaceAlt,
    overlay,
    well,
    wellDeep,
    panel,
    line,
    ink,
    inkSoft,
    inkFaint,
    onAccent,
    highlight
};

struct Accent
{
    juce::Colour base, hover, active;
};

//==============================================================================
inline juce::Colour surface (Surface s) noexcept
{
    switch (s)
    {
        case Surface::canvas:     return juce::Colour (0xffF5F7FB);
        case Surface::surface:    return juce::Colour (0xffffffff);
        case Surface::surfaceAlt: return juce::Colour (0xffEFF2F7);
        case Surface::overlay:    return juce::Colour (0xffffffff);
        case Surface::well:       return juce::Colour (0xffEBEEF4);
        case Surface::wellDeep:   return juce::Colour (0xffE6EAF1);
        case Surface::panel:      return juce::Colour (0xffF5F7FB);
        case Surface::line:       return juce::Colour (0xffE4E8EF);
        case Surface::ink:        return juce::Colour (0xff20242E);
        case Surface::inkSoft:    return juce::Colour (0xff565E6E);
        case Surface::inkFaint:   return juce::Colour (0xff7C8494);
        case Surface::onAccent:   return juce::Colour (0xffFAFBFE);
        case Surface::highlight:  return juce::Colour (0xffffffff);
    }
    return juce::Colours::transparentBlack;
}

inline Accent accent (Role r) noexcept
{
    switch (r)
    {
        case Role::global:    return { juce::Colour (0xff4C5FD5), juce::Colour (0xff3D4FBF), juce::Colour (0xff33429E) };
        case Role::frequency: return { juce::Colour (0xff0B7F79), juce::Colour (0xff096E69), juce::Colour (0xff075C58) };
        case Role::level:     return { juce::Colour (0xffAB5806), juce::Colour (0xff9A4E05), juce::Colour (0xff864404) };
        case Role::depth:     return { juce::Colour (0xffB02A6B), juce::Colour (0xff98215C), juce::Colour (0xff7E1B4C) };
        case Role::rate:      return { juce::Colour (0xff6E45C8), juce::Colour (0xff5C37AE), juce::Colour (0xff4B2B90) };
    }
    return accent (Role::global);
}

inline juce::Colour bandAccent (int band0) noexcept
{
    switch (band0 % 3)
    {
        case 1:  return juce::Colour (0xffA97800); // band 2 amber
        case 2:  return juce::Colour (0xffFFD400); // band 3 bright yellow
        default: return juce::Colour (0xffE15A4A); // band 1 coral (was pink #D62B7A)
    }
}

inline juce::Colour meterOk()   { return juce::Colour (0xff2E8B57); }
inline juce::Colour meterWarn() { return juce::Colour (0xff9C6B00); }
inline juce::Colour meterHot()  { return juce::Colour (0xffC42B2B); }

inline juce::Colour mix (juce::Colour a, juce::Colour b, float t) noexcept
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t));
}

inline juce::Colour blendOver (juce::Colour src, juce::Colour dst, float alpha) noexcept
{
    return mix (dst, src, alpha);
}

//==============================================================================
inline Section sectionForBand (int bandOneBased) noexcept
{
    switch (bandOneBased)
    {
        case 2:  return Section::band2;
        case 3:  return Section::band3;
        default: return Section::band1;
    }
}

inline juce::Colour sectionAccent (Section s) noexcept
{
    switch (s)
    {
        case Section::band1: return bandAccent (0);
        case Section::band2: return bandAccent (1);
        case Section::band3: return bandAccent (2);
        // Master is a container, not a frequency control; keep teal reserved
        // for frequency semantics (Formant knob / Xover) — batch-2 audit.
        case Section::master: return accent (Role::global).base;
        // T66: Growl is a texture page, not a frequency control.
        case Section::growl:  return accent (Role::depth).base;
        default: return accent (Role::global).base;
    }
}

inline juce::Colour panelFace (Section s) noexcept
{
    // Large panels read clearly brighter than the canvas: near-white with a
    // 1.5 % section tint.
    return mix (surface (Surface::surface), sectionAccent (s), 0.015f);
}

inline juce::Colour sectionBase (Section s) noexcept
{
    return mix (surface (Surface::surface), sectionAccent (s), 0.03f);
}

inline juce::Colour controlBase (Section s) noexcept
{
    // Buttons / combos sit one step darker than the panel so they are
    // identifiable without a border.
    return mix (sectionBase (s), surface (Surface::surfaceAlt), 0.55f);
}

inline juce::Colour sectionWell (Section s) noexcept
{
    return mix (surface (Surface::well), sectionAccent (s), 0.018f);
}

inline juce::Colour sectionStrip (Section s) noexcept
{
    return mix (surface (Surface::panel), sectionAccent (s), 0.025f);
}

inline juce::Colour shadowDark (Section s) noexcept
{
    return mix (juce::Colour (0xff2E3444), sectionAccent (s), 0.10f);
}

inline juce::Colour shadowLight() noexcept { return surface (Surface::highlight); }

inline juce::Colour disabledFace() noexcept
{
    return mix (surface (Surface::surfaceAlt), surface (Surface::canvas), 0.55f)
             .withMultipliedSaturation (0.35f);
}

inline juce::Colour disabledGlyph() noexcept
{
    return mix (surface (Surface::line), surface (Surface::inkSoft), 0.35f);
}

inline juce::Colour disabledText() noexcept
{
    return surface (Surface::inkFaint).withAlpha (0.75f);
}

inline juce::Colour transparent() noexcept { return juce::Colours::transparentBlack; }

//==============================================================================
// WCAG helpers — single implementation shared by drawing code and tests.
//==============================================================================
inline float relativeLuminance (juce::Colour c) noexcept
{
    const auto linear = [] (float v)
    {
        return v <= 0.04045f ? v / 12.92f
                             : std::pow ((v + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * linear (c.getFloatRed())
         + 0.7152f * linear (c.getFloatGreen())
         + 0.0722f * linear (c.getFloatBlue());
}

inline float contrastRatio (juce::Colour a, juce::Colour b) noexcept
{
    const auto l1 = relativeLuminance (a);
    const auto l2 = relativeLuminance (b);
    const auto hi = juce::jmax (l1, l2);
    const auto lo = juce::jmin (l1, l2);
    return (hi + 0.05f) / (lo + 0.05f);
}

//==============================================================================
// Section / role plumbing. Uses Component properties so the component tree
// and layout stay untouched; LookAndFeel drawing walks up the parent chain.
//==============================================================================
inline const juce::Identifier& sectionProperty()
{
    static const juce::Identifier id ("nvdSection");
    return id;
}

inline const juce::Identifier& roleProperty()
{
    static const juce::Identifier id ("nvdRole");
    return id;
}

inline void applySection (juce::Component& c, Section s)
{
    c.getProperties().set (sectionProperty(), static_cast<int> (s));
}

inline void applyRole (juce::Component& c, Role r)
{
    c.getProperties().set (roleProperty(), static_cast<int> (r));
}

inline Section sectionOf (const juce::Component& c) noexcept
{
    for (const auto* p = &c; p != nullptr; p = p->getParentComponent())
    {
        const auto& props = p->getProperties();
        if (props.contains (sectionProperty()))
        {
            const int idx = static_cast<int> (props[sectionProperty()]);
            if (idx >= static_cast<int> (Section::global)
                && idx <= static_cast<int> (Section::growl))
                return static_cast<Section> (idx);
        }
    }
    return Section::global;
}

inline Role roleOf (const juce::Component& c) noexcept
{
    for (const auto* p = &c; p != nullptr; p = p->getParentComponent())
    {
        const auto& props = p->getProperties();
        if (props.contains (roleProperty()))
        {
            const int idx = static_cast<int> (props[roleProperty()]);
            if (idx >= static_cast<int> (Role::global)
                && idx <= static_cast<int> (Role::rate))
                return static_cast<Role> (idx);
        }
    }
    return Role::global;
}

} // namespace ui::dopamine
