#include "ui/NeoLookAndFeel.h"

#include "BinaryData.h" // embedded Google Sans Code font (NeoVoiDriveData target)

#include <map>
#include <tuple>

namespace ui
{

namespace
{
NeoLookAndFeel* firstThemeInstance = nullptr; // installAsDefault instances
}

NeoLookAndFeel::~NeoLookAndFeel()
{
    if (! installAsDefault)
        return;

    // Remove ourselves from the theme list.
    if (firstThemeInstance == this)
        firstThemeInstance = nextThemeInstance;
    else
        for (auto* cur = firstThemeInstance; cur != nullptr; cur = cur->nextThemeInstance)
            if (cur->nextThemeInstance == this)
            {
                cur->nextThemeInstance = nextThemeInstance;
                break;
            }

    // If we are still the process-global default, hand it to another live
    // theme (or back to JUCE's built-in when none remain).
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == this)
        juce::LookAndFeel::setDefaultLookAndFeel (firstThemeInstance);
}

//==============================================================================
//  Global font: Google Sans Code variable font (MONO,wght axes), EMBEDDED as
//  binary data via juce_add_binary_data. Falls back to the JUCE default
//  sans-serif if the typeface data is somehow unusable (never crashes).
//==============================================================================
namespace
{
class EmbeddedFonts final : public juce::DeletedAtShutdown
{
public:
    EmbeddedFonts()
    {
        auto tf = juce::Typeface::createSystemTypefaceFor (
            NeoVoiDrive::GoogleSansCodeVariableFont_MONOwght_ttf,
            NeoVoiDrive::GoogleSansCodeVariableFont_MONOwght_ttfSize);
        if (tf == nullptr)
            return;

        const auto makeInstance = [tf] (float wght)
        {
            const juce::FontVariableSetting settings[] = {
                juce::FontVariableSetting (juce::FontFeatureTag ("MONO"), 1.0f),
                juce::FontVariableSetting (juce::FontFeatureTag ("wght"), wght)
            };
            return tf->cloneWithVariableSettings (settings);
        };

        regular = makeInstance (500.0f);
        medium  = makeInstance (600.0f);
        bold    = makeInstance (700.0f);
    }

    ~EmbeddedFonts() override { clearSingletonInstance(); }

    juce::Typeface::Ptr regular;
    juce::Typeface::Ptr medium;
    juce::Typeface::Ptr bold;

    JUCE_DECLARE_SINGLETON_INLINE (EmbeddedFonts, false)
};

juce::Typeface::Ptr loadEmbeddedFont (bool bold) noexcept
{
    auto* fonts = EmbeddedFonts::getInstance();
    return fonts == nullptr ? nullptr : (bold ? fonts->bold : fonts->regular);
}

//==============================================================================
//  Pre-rendered soft-shadow cache. Every caller is a paint routine on the
//  message thread; DropShadow is only rendered on a cache miss. Keyed by the
//  palette section so per-section shadow tints stay distinct.
//==============================================================================
struct ShadowCache
{
    struct Key
    {
        int w = 0, h = 0, radius = 0, blur = 0, dx = 0, dy = 0, alphaQ = 0;
        bool ellipse = false;
        bool light = false;
        int section = 0;

        bool operator< (const Key& o) const noexcept
        {
            return std::tie (w, h, radius, blur, dx, dy, alphaQ, ellipse, light, section)
                 < std::tie (o.w, o.h, o.radius, o.blur, o.dx, o.dy, o.alphaQ, o.ellipse, o.light, o.section);
        }
    };

    std::map<Key, juce::Image> images;
};

ShadowCache& shadowCache() noexcept
{
    // Leaked on purpose: a plain static would be destroyed during static
    // deinit, potentially after JUCE's image machinery.
    static auto* cache = new ShadowCache();
    return *cache;
}

const juce::Image& getShadowImage (const ShadowCache::Key& key)
{
    auto& images = shadowCache().images;
    if (const auto it = images.find (key); it != images.end())
        return it->second;

    if (images.size() > 256)
        images.clear();

    const auto margin = key.blur + juce::jmax (std::abs (key.dx), std::abs (key.dy));
    juce::Image img (juce::Image::ARGB, key.w, key.h, true);
    {
        juce::Graphics ig (img);
        juce::Path p;
        const juce::Rectangle<float> face (static_cast<float> (margin), static_cast<float> (margin),
                                           static_cast<float> (key.w) - 2.0f * (float) margin,
                                           static_cast<float> (key.h) - 2.0f * (float) margin);
        if (key.ellipse)
            p.addEllipse (face);
        else
            p.addRoundedRectangle (face, (float) key.radius);

        const auto section = static_cast<dopamine::Section> (key.section);
        const auto colour = key.light ? dopamine::shadowLight()
                                      : dopamine::shadowDark (section);
        juce::DropShadow (colour.withAlpha ((float) key.alphaQ / 255.0f),
                          key.blur, { key.dx, key.dy }).drawForPath (ig, p);
    }
    return images.emplace (key, std::move (img)).first->second;
}

void drawCachedShadow (juce::Graphics& g, juce::Rectangle<float> face, float radius,
                       float alpha, int blur, int dx, int dy, bool ellipse,
                       bool light, dopamine::Section section)
{
    if (face.getWidth() < 2.0f || face.getHeight() < 2.0f)
        return;

    const int margin = blur + juce::jmax (std::abs (dx), std::abs (dy));
    const ShadowCache::Key key {
        juce::roundToInt (std::ceil (face.getWidth())) + margin * 2,
        juce::roundToInt (std::ceil (face.getHeight())) + margin * 2,
        juce::roundToInt (radius), blur, dx, dy,
        juce::roundToInt (juce::jlimit (0.0f, 1.0f, alpha) * 255.0f),
        ellipse, light, static_cast<int> (section)
    };
    g.drawImageAt (getShadowImage (key),
                   juce::roundToInt (std::floor (face.getX())) - margin,
                   juce::roundToInt (std::floor (face.getY())) - margin);
}

juce::Font mediumFont (float height)
{
    if (auto tf = ui::NeoLookAndFeel::getMediumTypeface())
    {
        juce::Font f { juce::FontOptions (tf) };
        f.setHeight (height);
        return f;
    }
    return juce::Font { juce::FontOptions (height) };
}

// Batch 2: one focus-ring language for every custom-painted control. Focus is
// a system state, not a role, so it always uses Global indigo (palette: focus),
// plus a near-white inner keyline that stays visible on the indigo Bypass face
// and on saturated band tracks. Both strokes sit inside the face (no clipping).
void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> face, float radius)
{
    const auto focus = dopamine::accent (dopamine::Role::global).base;
    g.setColour (focus.withAlpha (0.65f));
    g.drawRoundedRectangle (face.reduced (1.0f), juce::jmax (2.0f, radius - 1.0f), 2.0f);
    g.setColour (dopamine::surface (dopamine::Surface::onAccent).withAlpha (0.35f));
    g.drawRoundedRectangle (face.reduced (3.5f), juce::jmax (1.5f, radius - 3.5f), 1.0f);
}

juce::Colour raisedTop (dopamine::Section section)
{
    return dopamine::mix (dopamine::controlBase (section),
                          dopamine::surface (dopamine::Surface::surface), 0.35f);
}

juce::Colour raisedBottom (dopamine::Section section)
{
    return dopamine::mix (dopamine::controlBase (section),
                          dopamine::shadowDark (section), 0.03f);
}
} // namespace

//==============================================================================
//  Neumorphic helpers.
//==============================================================================
float NeoLookAndFeel::neoBlur (juce::Rectangle<float> face) noexcept
{
    const auto minDim = juce::jmin (face.getWidth(), face.getHeight());
    return juce::jlimit (3.0f, 12.0f, 1.0f + 0.055f * minDim);
}

void NeoLookAndFeel::drawNeoShadowPair (juce::Graphics& g, juce::Rectangle<float> face, float radius,
                                        dopamine::Section section, bool ellipse, float alphaScale)
{
    if (face.getWidth() < 2.0f || face.getHeight() < 2.0f)
        return;

    const auto minDim = juce::jmin (face.getWidth(), face.getHeight());
    const int blur = juce::roundToInt (neoBlur (face));
    const int off = juce::jlimit (1, 3, juce::roundToInt (minDim * 0.025f));
    const auto scale = juce::jmax (0.0f, alphaScale);

    drawCachedShadow (g, face, radius, 0.22f * scale, juce::jmax (2, blur - 2),
                      -off, -off, ellipse, true, section);
    drawCachedShadow (g, face, radius, 0.075f * scale, blur,
                      off, off, ellipse, false, section);
}

void NeoLookAndFeel::drawNeoRaised (juce::Graphics& g, juce::Rectangle<float> face, float radius,
                                    dopamine::Section section, FaceState state, bool withShadow)
{
    const auto f = face.reduced (0.5f);
    if (f.getWidth() < 2.0f || f.getHeight() < 2.0f)
        return;

    if (state == FaceState::disabled)
    {
        if (withShadow)
            drawNeoShadowPair (g, f, radius, section, false, 0.18f);
        g.setColour (dopamine::disabledFace());
        g.fillRoundedRectangle (f, radius);
        return;
    }

    if (state == FaceState::pressed)
    {
        drawNeoInset (g, f, radius, section);
        return;
    }

    if (state == FaceState::on)
    {
        if (withShadow)
            drawNeoShadowPair (g, f, radius, section, false, 0.55f);

        const auto accent = dopamine::sectionAccent (section);
        const auto top = dopamine::mix (accent, dopamine::surface (dopamine::Surface::highlight), 0.10f);
        const auto bottom = dopamine::mix (accent, dopamine::accent (dopamine::Role::global).active, 0.25f);
        g.setGradientFill (juce::ColourGradient::vertical (top, f.getY(), bottom, f.getBottom()));
        g.fillRoundedRectangle (f, radius);

        const juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (f, radius);
        g.reduceClipRegion (clip);
        g.setColour (dopamine::shadowLight().withAlpha (0.14f));
        g.fillRect (f.getX(), f.getY(), f.getWidth(), 1.0f);
        g.fillRect (f.getX(), f.getY(), 1.0f, f.getHeight());
        return;
    }

    if (withShadow)
        drawNeoShadowPair (g, f, radius, section, false,
                           state == FaceState::hover ? 1.45f : 1.25f);

    const auto top = raisedTop (section);
    const auto bottom = raisedBottom (section);
    g.setGradientFill (juce::ColourGradient::vertical (top, f.getY(), bottom, f.getBottom()));
    g.fillRoundedRectangle (f, radius);

    // Soft top/left light — no 1 px border stroke.
    const juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (f, radius);
    g.reduceClipRegion (clip);
    g.setColour (dopamine::shadowLight().withAlpha (state == FaceState::hover ? 0.22f : 0.16f));
    g.fillRect (f.getX(), f.getY(), f.getWidth(), 1.0f);
    g.fillRect (f.getX(), f.getY(), 1.0f, f.getHeight());
}

void NeoLookAndFeel::drawNeoPanel (juce::Graphics& g, juce::Rectangle<float> face, float radius,
                                   dopamine::Section section, bool withShadow)
{
    const auto f = face.reduced (0.5f);
    if (f.getWidth() < 2.0f || f.getHeight() < 2.0f)
        return;

    if (withShadow)
        drawNeoShadowPair (g, f, radius, section, false, 0.7f);

    g.setColour (dopamine::panelFace (section));
    g.fillRoundedRectangle (f, radius);
}

void NeoLookAndFeel::drawNeoInset (juce::Graphics& g, juce::Rectangle<float> face, float radius,
                                   dopamine::Section section, bool shadingOnly)
{
    const auto f = face.reduced (0.5f);
    if (f.getWidth() < 2.0f || f.getHeight() < 2.0f)
        return;

    if (! shadingOnly)
    {
        g.setColour (dopamine::sectionWell (section));
        g.fillRoundedRectangle (f, radius);
    }

    const auto dark = dopamine::shadowDark (section);
    const auto light = dopamine::shadowLight();
    const auto depth = juce::jlimit (3.0f, 8.0f, 0.16f * juce::jmin (f.getWidth(), f.getHeight()));
    const auto sideDepth = depth * 0.8f;
    const auto lightDepth = depth * 0.6f;

    const juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (f, radius);
    g.reduceClipRegion (clip);

    // Top dark inner shadow.
    g.setGradientFill (juce::ColourGradient::vertical (dark.withAlpha (0.06f), f.getY(),
                                                        dark.withAlpha (0.0f), f.getY() + depth));
    g.fillRect (f.getX(), f.getY(), f.getWidth(), depth);

    // Left dark inner shadow.
    g.setGradientFill (juce::ColourGradient (dark.withAlpha (0.025f), f.getX(), f.getY(),
                                             dark.withAlpha (0.0f), f.getX() + sideDepth, f.getY(), false));
    g.fillRect (f.getX(), f.getY(), sideDepth, f.getHeight());

    // Bottom + right fill light.
    g.setGradientFill (juce::ColourGradient::vertical (light.withAlpha (0.0f), f.getBottom() - lightDepth,
                                                        light.withAlpha (0.18f), f.getBottom()));
    g.fillRect (f.getX(), f.getBottom() - lightDepth, f.getWidth(), lightDepth);

    g.setGradientFill (juce::ColourGradient (light.withAlpha (0.0f), f.getRight() - sideDepth, f.getY(),
                                             light.withAlpha (0.10f), f.getRight(), f.getY(), false));
    g.fillRect (f.getRight() - sideDepth, f.getY(), sideDepth, f.getHeight());
}

void NeoLookAndFeel::drawNeoConvexEllipse (juce::Graphics& g, juce::Rectangle<float> face,
                                           dopamine::Section section, FaceState state,
                                           juce::Colour tint)
{
    if (face.getWidth() < 2.0f || face.getHeight() < 2.0f)
        return;

    if (state == FaceState::disabled)
    {
        g.setColour (dopamine::disabledFace());
        g.fillEllipse (face);
        return;
    }

    drawCachedShadow (g, face, 0.0f, 0.08f, 3, 0, 1, true, false, section);
    drawCachedShadow (g, face, 0.0f, 0.20f, 2, -1, -1, true, true, section);

    auto base = dopamine::sectionBase (section);
    if (tint.getAlpha() > 0)
        base = dopamine::mix (base, tint, 0.10f);

    const auto top = dopamine::mix (base, dopamine::surface (dopamine::Surface::highlight),
                                    state == FaceState::hover ? 0.44f : 0.32f);
    const auto bottom = dopamine::mix (base, dopamine::shadowDark (section), 0.03f);
    g.setGradientFill (juce::ColourGradient::vertical (top, face.getY(), bottom, face.getBottom()));
    g.fillEllipse (face);

    const juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addEllipse (face);
    g.reduceClipRegion (clip);

    // Top-left light arc (soft, 2 px).
    juce::Path topLight;
    topLight.addArc (face.getX() + 1.5f, face.getY() + 1.5f,
                     face.getWidth() - 3.0f, face.getHeight() - 3.0f,
                     -juce::MathConstants<float>::pi * 0.42f,
                     juce::MathConstants<float>::pi * 0.42f, true);
    g.setColour (dopamine::shadowLight().withAlpha (0.24f));
    g.strokePath (topLight, juce::PathStrokeType (1.25f));

    // Bottom-right soft shade.
    juce::Path bottomShade;
    bottomShade.addArc (face.getX() + 1.0f, face.getY() + 1.0f,
                        face.getWidth() - 2.0f, face.getHeight() - 2.0f,
                        juce::MathConstants<float>::pi * 0.58f,
                        juce::MathConstants<float>::pi * 1.42f, true);
    g.setColour (dopamine::shadowDark (section).withAlpha (0.04f));
    g.strokePath (bottomShade, juce::PathStrokeType (1.25f));

    if (state == FaceState::hover)
    {
        g.setColour (dopamine::sectionAccent (section).withAlpha (0.04f));
        g.fillEllipse (face.reduced (face.getWidth() * 0.18f));
    }
}

//==============================================================================
//  Legacy wrappers.
//==============================================================================
void NeoLookAndFeel::drawRaisedFace (juce::Graphics& g, juce::Rectangle<float> face, float radius,
                                     bool highlighted, bool pressed)
{
    drawNeoRaised (g, face, radius, dopamine::Section::global,
                   pressed ? FaceState::pressed
                           : (highlighted ? FaceState::hover : FaceState::normal));
}

void NeoLookAndFeel::drawAccentFace (juce::Graphics& g, juce::Rectangle<float> face, float radius,
                                     bool pressed)
{
    const auto f = face.reduced (0.5f);
    if (f.getWidth() < 2.0f || f.getHeight() < 2.0f)
        return;

    const auto accent = dopamine::accent (dopamine::Role::global);
    if (pressed)
    {
        g.setColour (accent.active);
        g.fillRoundedRectangle (f, radius);
        return;
    }

    drawNeoShadowPair (g, f, radius, dopamine::Section::global, false, 0.65f);
    g.setGradientFill (juce::ColourGradient::vertical (accent.hover, f.getY(),
                                                        accent.active, f.getBottom()));
    g.fillRoundedRectangle (f, radius);

    const juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (f, radius);
    g.reduceClipRegion (clip);
        g.setColour (dopamine::shadowLight().withAlpha (0.20f));
        g.fillRect (f.getX(), f.getY(), f.getWidth(), 1.5f);
        g.fillRect (f.getX(), f.getY(), 1.5f, f.getHeight());
}

void NeoLookAndFeel::drawInsetWell (juce::Graphics& g, juce::Rectangle<float> face, float radius)
{
    drawNeoInset (g, face, radius, dopamine::Section::global, false);
}

void NeoLookAndFeel::drawInsetWellShading (juce::Graphics& g, juce::Rectangle<float> face, float radius)
{
    drawNeoInset (g, face, radius, dopamine::Section::global, true);
}

void NeoLookAndFeel::drawConvexEllipse (juce::Graphics& g, juce::Rectangle<float> face, bool enabled)
{
    drawNeoConvexEllipse (g, face, dopamine::Section::global,
                          enabled ? FaceState::normal : FaceState::disabled);
}

void NeoLookAndFeel::drawContactShadow (juce::Graphics& g, juce::Rectangle<float> face, float radius,
                                        float alpha, float drop)
{
    const juce::Graphics::ScopedSaveState save (g);
    g.setGradientFill ({ dopamine::shadowDark (dopamine::Section::global).withAlpha (alpha),
                         face.getX(), face.getBottom() - 2.0f,
                         dopamine::shadowDark (dopamine::Section::global).withAlpha (0.0f),
                         face.getX(), face.getBottom() + drop, false });
    g.fillRoundedRectangle (face.translated (0.0f, drop).reduced (0.5f, 0.0f), radius);
}

void NeoLookAndFeel::drawSoftShadow (juce::Graphics& g, juce::Rectangle<float> face, float radius,
                                     float alpha, int blur, int dy)
{
    drawCachedShadow (g, face, radius, alpha, blur, 0, dy, false, false,
                      dopamine::Section::global);
}

void NeoLookAndFeel::drawSoftShadowEllipse (juce::Graphics& g, juce::Rectangle<float> face,
                                            float alpha, int blur, int dy)
{
    drawCachedShadow (g, face, 0.0f, alpha, blur, 0, dy, true, false,
                      dopamine::Section::global);
}

//==============================================================================
NeoLookAndFeel::NeoLookAndFeel (bool installAsDefault)
    : installAsDefault (installAsDefault)
{
    // Font typeface resolution goes through the GLOBAL default L&F, so install
    // ourselves as the default; the dtor restores only while we are still the
    // current one (multi-editor safe).
    if (installAsDefault)
    {
        nextThemeInstance = firstThemeInstance;
        firstThemeInstance = this;
        juce::LookAndFeel::setDefaultLookAndFeel (this);
    }

    using namespace juce;

    // ---- colour-id mapping (everything resolves to the palette facade) ----
    setColour (ResizableWindow::backgroundColourId,        Tokens::background());
    setColour (DocumentWindow::textColourId,               Tokens::ink());
    setColour (PopupMenu::backgroundColourId,              dopamine::transparent());
    setColour (PopupMenu::textColourId,                    Tokens::ink());
    setColour (PopupMenu::headerTextColourId,              Tokens::inkSoft());
    setColour (PopupMenu::highlightedBackgroundColourId,   Tokens::surfaceAlt());
    setColour (PopupMenu::highlightedTextColourId,         Tokens::ink());
    setColour (ComboBox::backgroundColourId,               Tokens::surface());
    setColour (ComboBox::textColourId,                     Tokens::ink());
    setColour (ComboBox::arrowColourId,                    Tokens::inkSoft());
    setColour (ComboBox::outlineColourId,                  Tokens::line());
    setColour (ComboBox::buttonColourId,                   Tokens::surface());
    setColour (TextEditor::backgroundColourId,             Tokens::surface());
    setColour (TextEditor::textColourId,                   Tokens::ink());
    setColour (TextEditor::highlightColourId,              Tokens::surfaceAlt());
    setColour (TextEditor::highlightedTextColourId,        Tokens::ink());
    setColour (TextEditor::outlineColourId,                Tokens::line());
    setColour (TextEditor::focusedOutlineColourId,         Tokens::accent());
    setColour (Slider::textBoxBackgroundColourId,          dopamine::transparent());
    setColour (Slider::textBoxTextColourId,                Tokens::ink());
    setColour (Slider::textBoxHighlightColourId,           Tokens::surfaceAlt());
    setColour (Slider::textBoxOutlineColourId,             dopamine::transparent());
    setColour (Slider::trackColourId,                      Tokens::accent());
    setColour (Slider::backgroundColourId,                 Tokens::well());
    setColour (Slider::thumbColourId,                      Tokens::surface());
    setColour (Slider::rotarySliderOutlineColourId,        Tokens::line());
    setColour (Slider::rotarySliderFillColourId,           Tokens::accent());
    setColour (ToggleButton::textColourId,                 Tokens::ink());
    setColour (ToggleButton::tickColourId,                 Tokens::accent());
    setColour (ToggleButton::tickDisabledColourId,         Tokens::line());
    setColour (TextButton::buttonColourId,                 Tokens::surface());
    setColour (TextButton::buttonOnColourId,               Tokens::accent());
    setColour (TextButton::textColourOffId,                Tokens::ink());
    setColour (TextButton::textColourOnId,                 Tokens::onAccent());
    setColour (Label::textColourId,                        Tokens::ink());
    setColour (Label::textWhenEditingColourId,             Tokens::ink());
    setColour (Label::outlineWhenEditingColourId,          Tokens::accent());
    setColour (Label::backgroundWhenEditingColourId,       Tokens::surface());
    setColour (AlertWindow::backgroundColourId,            Tokens::surface());
    setColour (AlertWindow::textColourId,                  Tokens::ink());
    setColour (AlertWindow::outlineColourId,               Tokens::line());
    setColour (ListBox::backgroundColourId,                Tokens::surface());
    setColour (ListBox::textColourId,                      Tokens::ink());
    setColour (ListBox::outlineColourId,                   Tokens::line());
    setColour (ScrollBar::backgroundColourId,              Tokens::surface());
    setColour (ScrollBar::thumbColourId,                   Tokens::line());
    setColour (ScrollBar::trackColourId,                   Tokens::surface());
    setColour (TooltipWindow::backgroundColourId,          Tokens::surface());
    setColour (TooltipWindow::textColourId,                Tokens::ink());
    setColour (TooltipWindow::outlineColourId,             Tokens::line());
    setColour (Toolbar::backgroundColourId,                Tokens::surface());
    setColour (Toolbar::buttonMouseOverBackgroundColourId, Tokens::surfaceAlt());
    setColour (Toolbar::buttonMouseDownBackgroundColourId, Tokens::hover());
    setColour (TabbedComponent::backgroundColourId,         dopamine::transparent());
    setColour (TabbedComponent::outlineColourId,            dopamine::transparent());
    setColour (GroupComponent::textColourId,               Tokens::inkSoft());
    setColour (GroupComponent::outlineColourId,            Tokens::line());
    setColour (ProgressBar::backgroundColourId,            Tokens::well());
    setColour (ProgressBar::foregroundColourId,            Tokens::accent());
}

//==============================================================================
void NeoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider)
{
    using namespace juce;

    const auto bounds = Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height));
    drawKnobFace (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle,
                  slider.isEnabled(), slider.isMouseOverOrDragging(), slider.isMouseButtonDown(),
                  dopamine::sectionOf (slider), dopamine::roleOf (slider));
}

juce::Rectangle<float> NeoLookAndFeel::getKnobDiscBounds (const juce::Rectangle<float>& sliderBounds) noexcept
{
    const auto face = sliderBounds.reduced (4.0f);
    const auto radius = juce::jmin (face.getWidth(), face.getHeight()) * 0.5f;
    return { face.getCentreX() - radius, face.getCentreY() - radius,
             radius * 2.0f, radius * 2.0f };
}

juce::Rectangle<float> NeoLookAndFeel::getKnobDiscBounds (juce::Slider& slider)
{
    const auto sliderArea = slider.getLookAndFeel().getSliderLayout (slider).sliderBounds;
    return getKnobDiscBounds (sliderArea.toFloat())
               .translated (static_cast<float> (slider.getX()),
                            static_cast<float> (slider.getY()));
}

void NeoLookAndFeel::drawKnobFace (juce::Graphics& g, const juce::Rectangle<float>& bounds,
                                   float position, float startAngle, float endAngle,
                                   bool enabled, bool highlighted, bool dragging,
                                   dopamine::Section section, dopamine::Role role) noexcept
{
    using namespace juce;

    const auto disc   = getKnobDiscBounds (bounds);
    const auto radius = disc.getWidth() * 0.5f;
    const auto centre = disc.getCentre();
    const auto accent = dopamine::accent (role);

    drawNeoConvexEllipse (g, disc, section,
                          ! enabled ? FaceState::disabled
                                    : (highlighted ? FaceState::hover : FaceState::normal),
                          accent.base);

    // Inset groove: a 2.5 px rounded-cap band with a directional (top-left dark
    // → bottom-right light) gradient, sitting near the rim. The active arc uses
    // the same width and radius so groove and value bar read as one stroke.
    constexpr float trackWidth = 2.5f;
    const auto trackRadius = radius - 5.0f;
    if (trackRadius > 4.0f)
    {
        const Rectangle<float> trackBounds (centre.getX() - trackRadius, centre.getY() - trackRadius,
                                            trackRadius * 2.0f, trackRadius * 2.0f);
        Path trackPath;
        trackPath.addArc (trackBounds.getX(), trackBounds.getY(),
                          trackBounds.getWidth(), trackBounds.getHeight(),
                          startAngle, endAngle, true);

        Path stroked;
        PathStrokeType (trackWidth, PathStrokeType::curved, PathStrokeType::rounded)
            .createStrokedPath (stroked, trackPath);

        if (! enabled)
        {
            // A disabled knob still shows its full travel: filling the groove
            // with disabledFace() made it vanish into the face at value 0.
            g.setColour (dopamine::disabledGlyph().withAlpha (0.35f));
            g.fillPath (stroked);
        }
        else
        {
            const auto well = dopamine::sectionWell (section);
            const auto topLeft = dopamine::mix (well, dopamine::shadowDark (section), 0.28f);
            const auto mid = dopamine::mix (well, dopamine::shadowDark (section), 0.15f);
            // Keep the bottom-right tail at a mid grey (not near the knob
            // face), so the rounded end cap stays visible.
            const auto bottomRight = dopamine::mix (well, dopamine::shadowDark (section), 0.06f);

            const auto gb = trackBounds;
            juce::ColourGradient grad (topLeft, gb.getX(), gb.getY(),
                                       bottomRight, gb.getRight(), gb.getBottom(), false);
            grad.addColour (0.55, mid);
            g.setGradientFill (grad);
            g.fillPath (stroked);
        }

        if (position > 0.001f)
        {
            const auto arcColour = ! enabled ? dopamine::disabledGlyph()
                                  : dragging ? accent.active
                                  : highlighted ? accent.hover
                                                : accent.base;
            Path arc;
            arc.addArc (trackBounds.getX(), trackBounds.getY(),
                        trackBounds.getWidth(), trackBounds.getHeight(),
                        startAngle, startAngle + position * (endAngle - startAngle), true);
            g.setColour (arcColour);
            g.strokePath (arc, PathStrokeType (trackWidth, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    if (highlighted && enabled)
    {
        g.setColour (accent.base.withAlpha (0.04f));
        g.fillEllipse (disc.reduced (radius * 0.28f));
    }
}

//==============================================================================
void NeoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float minSliderPos, float maxSliderPos,
                                       juce::Slider::SliderStyle style, juce::Slider& slider)
{
    using namespace juce;

    ignoreUnused (minSliderPos, maxSliderPos);

    const auto isHorizontal = (style == Slider::LinearHorizontal || style == Slider::LinearBar);
    const auto bounds = Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height));
    const auto section = dopamine::sectionOf (slider);
    const auto role = dopamine::roleOf (slider);
    const auto accent = dopamine::accent (role);

    const auto trackThickness = 6.0f;
    const auto thumbRadius = static_cast<float> (getSliderThumbRadius (slider));
    const auto enabled = slider.isEnabled();
    const auto accentColour = enabled
                            ? (slider.isMouseButtonDown() ? accent.active : accent.base)
                            : dopamine::disabledGlyph();

    if (isHorizontal)
    {
        auto track = Rectangle<float> (bounds.getX() + thumbRadius,
                                       bounds.getCentreY() - trackThickness * 0.5f,
                                       bounds.getWidth() - thumbRadius * 2.0f, trackThickness);
        if (enabled)
            drawNeoInset (g, track, trackThickness * 0.5f, section);
        else
        {
            g.setColour (dopamine::disabledFace());
            g.fillRoundedRectangle (track, trackThickness * 0.5f);
        }

        const auto pos = jlimit (bounds.getX() + thumbRadius, bounds.getRight() - thumbRadius, sliderPos);
        auto active = track;
        active.setWidth (jmax (0.0f, pos - track.getX()));
        g.setColour (accentColour);
        g.fillRoundedRectangle (active, trackThickness * 0.5f);

        const Rectangle<float> thumb (pos - thumbRadius, bounds.getCentreY() - thumbRadius,
                                      thumbRadius * 2.0f, thumbRadius * 2.0f);
        drawNeoConvexEllipse (g, thumb, section, enabled ? FaceState::normal : FaceState::disabled);
    }
    else
    {
        auto track = Rectangle<float> (bounds.getCentreX() - trackThickness * 0.5f,
                                       bounds.getY() + thumbRadius,
                                       trackThickness, bounds.getHeight() - thumbRadius * 2.0f);
        if (enabled)
            drawNeoInset (g, track, trackThickness * 0.5f, section);
        else
        {
            g.setColour (dopamine::disabledFace());
            g.fillRoundedRectangle (track, trackThickness * 0.5f);
        }

        const auto pos = jlimit (bounds.getY() + thumbRadius, bounds.getBottom() - thumbRadius, sliderPos);
        auto active = track;
        active.setY (pos);
        active.setHeight (jmax (0.0f, track.getBottom() - pos));
        g.setColour (accentColour);
        g.fillRoundedRectangle (active, trackThickness * 0.5f);

        const Rectangle<float> thumb (bounds.getCentreX() - thumbRadius, pos - thumbRadius,
                                      thumbRadius * 2.0f, thumbRadius * 2.0f);
        drawNeoConvexEllipse (g, thumb, section, enabled ? FaceState::normal : FaceState::disabled);
    }

    if (slider.isMouseOverOrDragging() && slider.isEnabled())
    {
        g.setColour (accent.base.withAlpha (0.08f));
        g.fillRoundedRectangle (bounds, Tokens::radiusSmall);
    }
}

//==============================================================================
int NeoLookAndFeel::getSliderThumbRadius (juce::Slider&) { return 8; }

//==============================================================================
void NeoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                           const juce::Colour&, bool highlighted, bool down)
{
    using namespace juce;

    const auto bounds = button.getLocalBounds().toFloat();
    const auto radius = jmin (bounds.getHeight() * 0.5f, Tokens::radiusButton);
    const auto face = bounds.reduced (2.0f);
    const auto section = dopamine::sectionOf (button);
    const auto role = dopamine::roleOf (button);
    const auto accent = dopamine::accent (role);

    if (! button.isEnabled())
    {
        drawNeoShadowPair (g, face, radius, section, false, 0.35f);
        g.setColour (dopamine::disabledFace());
        g.fillRoundedRectangle (face, radius);
        return;
    }

    if (button.getToggleState())
    {
        // On = accent tint face (dopamine pop) with onAccent text.
        drawNeoShadowPair (g, face, radius, section, false, 0.7f);
        g.setGradientFill (juce::ColourGradient::vertical (accent.hover, face.getY(),
                                                            accent.active, face.getBottom()));
        g.fillRoundedRectangle (face, radius);

        const juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (face, radius);
        g.reduceClipRegion (clip);
        g.setColour (dopamine::shadowLight().withAlpha (0.12f));
        g.fillRect (face.getX(), face.getY(), face.getWidth(), 1.0f);
        g.fillRect (face.getX(), face.getY(), 1.0f, face.getHeight());

        if (button.hasKeyboardFocus (true))
            drawFocusRing (g, face, radius);
        return;
    }

    if (down)
    {
        drawNeoInset (g, face, radius, section);
        if (button.hasKeyboardFocus (true))
            drawFocusRing (g, face, radius);
        return;
    }

    drawNeoRaised (g, face, radius, section,
                   highlighted ? FaceState::hover : FaceState::normal);
    if (button.hasKeyboardFocus (true))
        drawFocusRing (g, face, radius);
}

//==============================================================================
void NeoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool highlighted, bool down)
{
    using namespace juce;

    const auto bounds  = button.getLocalBounds().toFloat();
    const auto on      = button.getToggleState();
    const auto enabled = button.isEnabled();
    const auto section = dopamine::sectionOf (button);
    const auto role    = dopamine::roleOf (button);
    // Batch 2: a switch shows what it enables — an explicit role (Formant =
    // frequency) wins over the container colour; band switches carry their
    // band section instead.
    const auto accent = role != dopamine::Role::global
                          ? dopamine::accent (role).base
                          : dopamine::sectionAccent (section);

    if (button.getButtonText().isEmpty())
    {
        const auto trackW = jmin (30.0f, bounds.getWidth());
        const auto trackH = jmin (18.0f, bounds.getHeight());
        const Rectangle<float> track (bounds.getCentreX() - trackW * 0.5f,
                                      bounds.getCentreY() - trackH * 0.5f,
                                      trackW, trackH);
        const auto r = trackH * 0.5f;

        if (! enabled)
        {
            g.setColour (dopamine::disabledFace());
            g.fillRoundedRectangle (track, r);
        }
        else if (on)
        {
            if (highlighted)
            {
                g.setColour (accent.withAlpha (0.05f));
                g.fillRoundedRectangle (track.expanded (2.0f), r + 2.0f);
            }

            g.setGradientFill (juce::ColourGradient::vertical (accent, track.getY(),
                                                                dopamine::mix (accent, dopamine::shadowDark (section), 0.12f),
                                                                track.getBottom()));
            g.fillRoundedRectangle (track, r);

            const Graphics::ScopedSaveState save (g);
            Path clip;
            clip.addRoundedRectangle (track, r);
            g.reduceClipRegion (clip);
            g.setGradientFill (juce::ColourGradient::vertical (dopamine::shadowDark (section).withAlpha (0.07f),
                                                                track.getY(),
                                                                dopamine::shadowDark (section).withAlpha (0.0f),
                                                                track.getY() + 4.0f));
            g.fillRect (track.getX(), track.getY(), track.getWidth(), 4.0f);
            g.setColour (dopamine::shadowLight().withAlpha (0.10f));
            g.fillRect (track.getX(), track.getBottom() - 1.0f, track.getWidth(), 1.0f);
        }
        else
        {
            drawNeoInset (g, track, r, section);
        }

        const auto knobD = trackH - 4.0f;
        const auto knobX = on ? track.getRight() - 2.0f - knobD : track.getX() + 2.0f;
        const Rectangle<float> knob (knobX, track.getCentreY() - knobD * 0.5f, knobD, knobD);
        drawNeoConvexEllipse (g, knob, section, enabled ? FaceState::normal : FaceState::disabled);

        if (enabled && button.hasKeyboardFocus (true))
            drawFocusRing (g, bounds.reduced (1.0f),
                           jmin (bounds.getHeight() * 0.5f, 10.0f));

        ignoreUnused (down);
        return;
    }

    // ---- radio dot + text ----
    // T64 follow-up: give the whole radio a visible capsule so unselected
    // options (In / Out / Both, 1/2/3 bands) read as buttons.
    const auto pill = bounds.reduced (1.0f);
    const auto pillR = jmin (pill.getHeight() * 0.5f, 12.0f);
    if (! enabled)
    {
        g.setColour (dopamine::disabledFace());
    }
    else if (on)
    {
        g.setColour (dopamine::mix (dopamine::surface (dopamine::Surface::surface),
                                    dopamine::sectionAccent (section), 0.12f));
    }
    else
    {
        g.setColour (highlighted
                         ? dopamine::mix (dopamine::controlBase (section),
                                          dopamine::surface (dopamine::Surface::surface), 0.35f)
                         : dopamine::controlBase (section));
    }
    g.fillRoundedRectangle (pill, pillR);

    if (enabled)
    {
        const juce::Graphics::ScopedSaveState clipState (g);
        juce::Path pillClip;
        pillClip.addRoundedRectangle (pill, pillR);
        g.reduceClipRegion (pillClip);
        g.setColour (dopamine::shadowLight().withAlpha (0.30f));
        g.fillRect (pill.getX(), pill.getY(), pill.getWidth(), 1.0f);
    }

    const auto dotD = jmin (16.0f, bounds.getHeight() - 4.0f);
    const Rectangle<float> dot (bounds.getX() + 3.0f, bounds.getCentreY() - dotD * 0.5f, dotD, dotD);
    const auto dotAccent = dopamine::accent (role);
    const auto sectionAccent = dopamine::sectionAccent (section);

    if (on)
    {
        const auto c = enabled ? sectionAccent : dopamine::disabledGlyph();
        g.setGradientFill (juce::ColourGradient::vertical (c, dot.getY(),
                                                            dopamine::mix (c, dopamine::shadowDark (section), 0.20f),
                                                            dot.getBottom()));
        g.fillEllipse (dot);
        if (enabled)
        {
            g.setColour (dopamine::surface (dopamine::Surface::onAccent));
            g.fillEllipse (dot.getCentreX() - 2.5f, dot.getCentreY() - 2.5f, 5.0f, 5.0f);
        }
    }
    else
    {
        drawNeoConvexEllipse (g, dot, section, enabled ? FaceState::normal : FaceState::disabled);
    }

    if (highlighted && enabled)
    {
        g.setColour (dotAccent.base.withAlpha (0.05f));
        g.fillEllipse (dot.expanded (2.0f));
    }

    // Same emphasis face as buttons/tabs: radios were the only 14 px regular
    // control text in the UI, which read as a lighter hierarchy level.
    g.setFont (mediumFont (14.0f));
    g.setColour (! enabled ? dopamine::disabledText()
                 : on      ? dopamine::mix (sectionAccent, dopamine::surface (dopamine::Surface::ink), 0.35f)
                           : dopamine::surface (dopamine::Surface::inkSoft));
    const auto textX = dot.getRight() + 6.0f;
    g.drawText (button.getButtonText(),
                Rectangle<float> (textX, bounds.getY(),
                                  jmax (0.0f, bounds.getRight() - textX), bounds.getHeight()),
                Justification::centredLeft, false);

    if (enabled && button.hasKeyboardFocus (true))
        drawFocusRing (g, pill, pillR);

    ignoreUnused (down);
}

//==============================================================================
void NeoLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component,
                                  float x, float y, float w, float h,
                                  bool ticked, bool isEnabled,
                                  bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    using namespace juce;

    const auto box = Rectangle<float> (x, y, w, h).reduced (2.0f);
    const auto section = dopamine::sectionOf (component);

    if (ticked)
    {
        const auto accent = dopamine::sectionAccent (section);
        g.setGradientFill (juce::ColourGradient::vertical (accent, box.getY(),
                                                            dopamine::mix (accent, dopamine::shadowDark (section), 0.20f),
                                                            box.getBottom()));
        g.fillRoundedRectangle (box, 5.0f);
    }
    else
    {
        drawNeoInset (g, box, 5.0f, section);
    }

    if (ticked)
    {
        Path check;
        check.startNewSubPath (box.getX() + box.getWidth() * 0.22f, box.getCentreY());
        check.lineTo (box.getX() + box.getWidth() * 0.44f, box.getY() + box.getHeight() * 0.68f);
        check.lineTo (box.getX() + box.getWidth() * 0.80f, box.getY() + box.getHeight() * 0.30f);
        g.setColour (dopamine::surface (dopamine::Surface::onAccent));
        g.strokePath (check, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    ignoreUnused (isEnabled, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

//==============================================================================
void NeoLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                   int buttonX, int buttonY, int buttonW, int buttonH,
                                   juce::ComboBox& box)
{
    using namespace juce;

    const auto bounds = Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    const auto radius = jmin (height * 0.5f, Tokens::radiusButton);
    const auto face = bounds.reduced (2.0f);
    const auto section = dopamine::sectionOf (box);
    const auto role = dopamine::roleOf (box);
    const auto accent = dopamine::accent (role);

    if (! box.isEnabled())
    {
        drawNeoShadowPair (g, face, radius, section, false, 0.35f);
        g.setColour (dopamine::disabledFace());
        g.fillRoundedRectangle (face, radius);
    }
    else if (isButtonDown)
    {
        drawNeoInset (g, face, radius, section);
    }
    else
    {
        drawNeoRaised (g, face, radius, section,
                       box.isMouseOverOrDragging() ? FaceState::hover : FaceState::normal);
    }

    const auto arrowSize = 5.0f;
    const auto arrowCx = static_cast<float> (buttonX) + static_cast<float> (buttonW) * 0.5f;
    const auto arrowCy = static_cast<float> (buttonY) + static_cast<float> (buttonH) * 0.5f;
    Path arrow;
    arrow.startNewSubPath (arrowCx - arrowSize, arrowCy - arrowSize * 0.5f);
    arrow.lineTo (arrowCx, arrowCy + arrowSize * 0.5f);
    arrow.lineTo (arrowCx + arrowSize, arrowCy - arrowSize * 0.5f);
    // Batch 2: the chevron carries the control's semantic accent (Wave = rate
    // violet, Slope = frequency teal, UI Size = global indigo) so combos are
    // not the only colourless control class.
    const auto arrowColour = box.isEnabled()
                               ? (isButtonDown ? accent.active
                                 : box.isMouseOverOrDragging() ? accent.hover
                                                               : accent.base)
                               : dopamine::disabledGlyph();
    g.setColour (arrowColour);
    g.strokePath (arrow, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));

    if (box.isEnabled() && box.hasKeyboardFocus (true))
        drawFocusRing (g, face, radius);
}

//==============================================================================
void NeoLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                        bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcutKeyText,
                                        const juce::Drawable* icon, const juce::Colour* textColour)
{
    using namespace juce;
    const auto s = uiScale;

    if (isSeparator)
    {
        // Soft groove instead of a 1 px rule.
        const auto r = area.reduced (roundToInt (20.0f * s), 0).toFloat();
        const auto y = r.getCentreY() - 1.5f * s;
        g.setGradientFill (juce::ColourGradient::horizontal (
            dopamine::shadowDark (dopamine::Section::global).withAlpha (0.0f), r.getX(),
            dopamine::shadowDark (dopamine::Section::global).withAlpha (0.16f), r.getCentreX()));
        g.fillRect (r.getX(), y, r.getWidth() * 0.5f, 3.0f * s);
        g.setGradientFill (juce::ColourGradient::horizontal (
            dopamine::shadowDark (dopamine::Section::global).withAlpha (0.16f), r.getCentreX(),
            dopamine::shadowDark (dopamine::Section::global).withAlpha (0.0f), r.getRight()));
        g.fillRect (r.getCentreX(), y, r.getWidth() * 0.5f, 3.0f * s);
        return;
    }

    const auto bounds = area.toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (dopamine::mix (dopamine::sectionBase (dopamine::Section::global),
                                    dopamine::accent (dopamine::Role::global).base, 0.08f));
        g.fillRoundedRectangle (bounds.reduced (20.0f * s, 2.0f * s), Tokens::radiusSmall * s);
    }

    if (icon != nullptr)
    {
        const auto iconArea = Rectangle<float> (bounds.getX() + 24.0f * s, bounds.getY(),
                                                bounds.getHeight(), bounds.getHeight());
        icon->drawWithin (g, iconArea.reduced (5.0f * s), RectanglePlacement::centred, 1.0f);
    }
    else if (isTicked)
    {
        // Tick lives in the left gutter so ticked and unticked rows share the
        // same text origin (the old x=44 shifted the selected row right).
        g.setColour (dopamine::accent (dopamine::Role::global).base);
        g.fillEllipse (bounds.getX() + 22.0f * s, bounds.getCentreY() - 2.5f * s,
                       5.0f * s, 5.0f * s);
    }

    const auto textColourToUse = textColour != nullptr ? *textColour
                                  : (isActive ? dopamine::surface (dopamine::Surface::ink)
                                              : dopamine::surface (dopamine::Surface::inkFaint));
    g.setColour (textColourToUse);
    g.setFont (getPopupMenuFont());

    const auto textX = bounds.getX() + (icon != nullptr ? 44.0f : 30.0f) * s;
    g.drawFittedText (text, Rectangle<int> (static_cast<int> (textX), area.getY(),
                                            jmax (0, area.getWidth() - static_cast<int> (textX)
                                                       - roundToInt (20.0f * s)),
                                            area.getHeight()),
                      Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (dopamine::surface (dopamine::Surface::inkSoft));
        g.setFont (getPopupMenuFont().withHeight (getPopupMenuFont().getHeight() * 0.85f));
        const auto shortcutRight = area.getRight() - roundToInt ((hasSubMenu ? 36.0f : 26.0f) * s);
        g.drawFittedText (shortcutKeyText,
                          juce::Rectangle<int> (shortcutRight - roundToInt (60.0f * s), area.getY(),
                                                roundToInt (60.0f * s), area.getHeight()),
                          Justification::centredRight, 1);
    }

    if (hasSubMenu)
    {
        g.setColour (dopamine::surface (dopamine::Surface::inkSoft));
        const auto cx = bounds.getRight() - 24.0f * s;
        Path arrow;
        arrow.startNewSubPath (cx - 3.0f * s, bounds.getCentreY() - 4.0f * s);
        arrow.lineTo (cx + 2.0f * s, bounds.getCentreY());
        arrow.lineTo (cx - 3.0f * s, bounds.getCentreY() + 4.0f * s);
        g.strokePath (arrow, PathStrokeType (1.5f * s, PathStrokeType::curved,
                                             PathStrokeType::rounded));
    }
}

//==============================================================================
void NeoLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));

    // System drop shadow stays OFF; the overlay face + self-drawn shadow
    // separate the menu from the plugin canvas. The item area is inset by
    // getPopupMenuBorderSize() so text clears the shadow.
    const auto s = uiScale;
    const auto popupRadius = 16.0f * s;
    const auto face = bounds.reduced (14.0f * s);
    const auto section = dopamine::Section::global;

    // T65 follow-up (user: add a shadow to the dropdown menu, not the box):
    // in the real popup window the cached image shadows did not appear
    // (they paint fine off-screen), so the projection is built from
    // accumulated translucent fills: densest against the face, fading to
    // nothing before the 14 px border ends, offset down/right.
    const auto shadowColour = dopamine::mix (dopamine::shadowDark (section),
                                             dopamine::surface (dopamine::Surface::ink), 0.35f);
    constexpr int shadowSteps = 18;
    const auto shadowSpread = 12.0f * s;
    for (int i = shadowSteps; i >= 1; --i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (shadowSteps);
        const float alpha = 0.040f * (1.0f - t) * (1.0f - t) + 0.004f;
        const auto spread = face.expanded (shadowSpread * t).translated (1.0f * s, 2.0f * s);
        g.setColour (shadowColour.withAlpha (alpha));
        g.fillRoundedRectangle (spread, popupRadius + shadowSpread * t);
    }

    const auto base = dopamine::surface (dopamine::Surface::overlay);
    const auto bottom = dopamine::mix (base, dopamine::sectionBase (section), 0.04f);
    g.setGradientFill (juce::ColourGradient::vertical (base, face.getY(), bottom, face.getBottom()));
    g.fillRoundedRectangle (face, popupRadius);

    const juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (face, popupRadius);
    g.reduceClipRegion (clip);
    g.setColour (dopamine::shadowLight().withAlpha (0.35f));
    g.fillRect (face.getX(), face.getY(), face.getWidth(), 1.0f * s);
    g.fillRect (face.getX(), face.getY(), 1.0f * s, face.getHeight());
}

//==============================================================================
void NeoLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                            juce::TextEditor& editor)
{
    using namespace juce;

    const auto bounds = Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));

    if (! editor.hasKeyboardFocus (true))
        return;

    // Soft focus glow (no hard outline).
    const auto accent = dopamine::accent (dopamine::Role::global).base;
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (accent.withAlpha (0.16f - 0.04f * static_cast<float> (i)));
        g.drawRoundedRectangle (bounds.reduced (0.5f + static_cast<float> (i)),
                                Tokens::radiusSmall, 2.0f);
    }
}

//==============================================================================
void NeoLookAndFeel::drawLevelMeter (juce::Graphics& g, int width, int height, float level)
{
    using namespace juce;

    const auto bounds = Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    drawNeoInset (g, bounds, bounds.getHeight() * 0.5f, dopamine::Section::global);

    constexpr int numSegments = 24;
    const auto gap = 2.0f;
    const auto inner = bounds.reduced (1.5f, 1.5f);
    const auto segW = (inner.getWidth() - gap * static_cast<float> (numSegments - 1))
                    / static_cast<float> (numSegments);

    const auto clamped = jlimit (0.0f, 1.0f, level);
    const auto active = static_cast<int> (std::ceil (clamped * numSegments));

    for (int i = 0; i < active; ++i)
    {
        const auto frac = static_cast<float> (i + 1) / static_cast<float> (numSegments);
        const auto colour = frac <= 0.70f ? dopamine::meterOk()
                          : frac <= 0.90f ? dopamine::meterWarn()
                          : dopamine::meterHot();
        g.setColour (colour);
        g.fillRoundedRectangle (inner.getX() + static_cast<float> (i) * (segW + gap),
                                inner.getY(), segW, inner.getHeight(), 1.5f);
    }
}

//==============================================================================
void NeoLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y,
                                    int width, int height, bool isScrollbarVertical,
                                    int thumbStartPosition, int thumbSize,
                                    bool isMouseOver, bool isMouseDown)
{
    using namespace juce;

    const auto bounds = Rectangle<int> (x, y, width, height).toFloat();
    const auto thumb = isScrollbarVertical
                     ? Rectangle<float> (bounds.getX() + 2.0f, static_cast<float> (thumbStartPosition),
                                         bounds.getWidth() - 4.0f, static_cast<float> (thumbSize))
                     : Rectangle<float> (static_cast<float> (thumbStartPosition), bounds.getY() + 2.0f,
                                         static_cast<float> (thumbSize), bounds.getHeight() - 4.0f);

    const auto section = dopamine::Section::global;
    if (isMouseOver || isMouseDown)
        drawNeoShadowPair (g, thumb, 4.0f, section, false, 0.7f);

    g.setColour (dopamine::mix (dopamine::sectionBase (section),
                                dopamine::shadowDark (section),
                                isMouseOver || isMouseDown ? 0.10f : 0.04f));
    g.fillRoundedRectangle (thumb, 4.0f);
}

//==============================================================================
juce::Typeface::Ptr NeoLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (auto tf = getGlobalTypeface (font.isBold()))
        return tf;
    return LookAndFeel_V4::getTypefaceForFont (font);
}

juce::Typeface::Ptr NeoLookAndFeel::getGlobalTypeface (bool bold) noexcept
{
    return loadEmbeddedFont (bold);
}

juce::Typeface::Ptr NeoLookAndFeel::getMediumTypeface() noexcept
{
    auto* fonts = EmbeddedFonts::getInstance();
    return fonts == nullptr ? nullptr : fonts->medium;
}

//==============================================================================
juce::Font NeoLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (16.0f * uiScale));
}

juce::Font NeoLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return mediumFont (14.0f);
}

//==============================================================================
int NeoLookAndFeel::getPopupMenuBorderSize()
{
    return juce::roundToInt (14.0f * uiScale);
}

juce::PopupMenu::Options NeoLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box,
                                                                         juce::Label& label)
{
    return juce::LookAndFeel_V2::getOptionsForComboBoxPopupMenu (box, label)
        .withStandardItemHeight (juce::roundToInt (static_cast<float> (label.getHeight()) * uiScale));
}

//==============================================================================
void NeoLookAndFeel::setUiScale (float scale) noexcept
{
    uiScale = juce::jlimit (0.5f, 2.5f, scale);
}

//==============================================================================
void NeoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                     bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    g.setFont (mediumFont (14.0f));

    const auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                                   : juce::TextButton::textColourOffId)
                            .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f);
    g.setColour (colour);
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().reduced (4, 0),
                      juce::Justification::centred, 1);

    juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
}

//==============================================================================
//  Band tabs: flat full-width segments fused with the card; no 1 px
//  separators — volume comes from the section tint and soft edge gradients.
//==============================================================================
void NeoLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                                    bool isMouseOver, bool isMouseDown)
{
    using namespace juce;

    const auto b = button.getLocalBounds().toFloat();
    const auto front = button.isFrontTab();
    const auto section = dopamine::sectionOf (button);
    const auto accent = dopamine::sectionAccent (section);

    const bool first = button.getIndex() == 0;
    const bool last  = button.getIndex() == button.getTabbedButtonBar().getNumTabs() - 1;
    constexpr float r = Tokens::radiusTab;

    auto f = b.withTrimmedTop (1.0f);
    if (first) f.setLeft (b.getX() + 1.0f);
    if (last)  f.setRight (b.getRight() - 1.0f);

    const auto faceColour = front
                          ? dopamine::mix (dopamine::sectionBase (section),
                                           dopamine::surface (dopamine::Surface::surface), 0.40f)
                          : isMouseDown ? dopamine::mix (dopamine::sectionWell (section),
                                                         dopamine::surface (dopamine::Surface::surface), 0.25f)
                          : isMouseOver ? dopamine::mix (dopamine::sectionBase (section),
                                                         dopamine::surface (dopamine::Surface::surface), 0.35f)
                                        : dopamine::sectionWell (section);

    if (first || last)
    {
        Path face;
        face.startNewSubPath (f.getX(), f.getBottom());
        if (first)
        {
            face.lineTo (f.getX(), f.getY() + r);
            face.addArc (f.getX(), f.getY(), r * 2.0f, r * 2.0f,
                         -MathConstants<float>::halfPi, 0.0f, false);
        }
        else
        {
            face.lineTo (f.getX(), f.getY());
        }
        if (last)
        {
            face.lineTo (f.getRight() - r, f.getY());
            face.addArc (f.getRight() - r * 2.0f, f.getY(), r * 2.0f, r * 2.0f,
                         0.0f, MathConstants<float>::halfPi, false);
        }
        else
        {
            face.lineTo (f.getRight(), f.getY());
        }
        face.lineTo (f.getRight(), f.getBottom());
        face.closeSubPath();
        g.setColour (faceColour);
        g.fillPath (face);
    }
    else
    {
        g.setColour (faceColour);
        g.fillRect (f);
    }

    // Front tab: flat face, no gradient/top highlight (panel background).
    // Soft vertical edge shade between segments (replaces the 1 px line).
    if (! last)
    {
        g.setGradientFill (juce::ColourGradient::horizontal (
            dopamine::shadowDark (section).withAlpha (0.08f), b.getRight() - 3.0f,
            dopamine::shadowDark (section).withAlpha (0.0f), b.getRight() + 1.0f));
        g.fillRect (b.getRight() - 3.0f, b.getY() + 4.0f, 4.0f, b.getHeight() - 8.0f);
    }

    // Selected tab accent pill (dopamine identity, not a border).
    if (front)
    {
        const Rectangle<float> pill (b.getCentreX() - 8.0f, b.getBottom() - 4.0f, 16.0f, 3.0f);
        g.setColour (accent.withAlpha (0.9f));
        g.fillRoundedRectangle (pill, 1.5f);
    }

    g.setFont (mediumFont (14.0f));
    g.setColour (front ? dopamine::surface (dopamine::Surface::ink)
                       : dopamine::surface (dopamine::Surface::inkSoft));
    // T73: centre the label in the area right of the Band On switch. Keep
    // enough width for the 14 px label (no horizontal squeezing) while a
    // modest right inset prevents the label from sitting too far right.
    g.drawFittedText (button.getButtonText(),
                      button.getTextArea().withTrimmedRight (20).reduced (2, 0),
                      Justification::centred, 1);

    // Only the tab itself: a focused Band On switch draws its own ring, so
    // the tab must not add a second one (hasKeyboardFocus(false) excludes the
    // extra component).
    if (button.hasKeyboardFocus (false))
        drawFocusRing (g, f.reduced (2.0f), 8.0f);
}

int NeoLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth)
{
    juce::ignoreUnused (tabDepth);
    auto& bar = button.getTabbedButtonBar();
    const int n = juce::jmax (1, bar.getNumTabs());
    const int w = bar.getWidth();
    if (w <= 0)
        return 60;
    const int base = w / n;
    return button.getIndex() == n - 1 ? w - base * (n - 1) : base;
}

int NeoLookAndFeel::getTabButtonOverlap (int tabDepth)
{
    juce::ignoreUnused (tabDepth);
    return 0;
}

int NeoLookAndFeel::getTabButtonSpaceAroundImage()
{
    return 0;
}

void NeoLookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&,
                                                   int w, int h)
{
    juce::ignoreUnused (w, h);
}

} // namespace ui
