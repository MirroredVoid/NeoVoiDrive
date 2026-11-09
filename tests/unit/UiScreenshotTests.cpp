#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdlib>
#include <utility>

#if defined(_MSC_VER)
#pragma warning (push)
#pragma warning (disable : 4996) // getenv is fine here (test-only)
#endif

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/BottomBarComponent.h"
#include "ui/CrossoverDisplay.h"
#include "ui/DopaminePalette.h"
#include "ui/TopBarComponent.h"

namespace
{

// T64: screenshot probes are palette-derived (no hard-coded RGB thresholds).
int rgbDistance (juce::Colour a, juce::Colour b) noexcept
{
    return std::abs (a.getRed() - b.getRed())
         + std::abs (a.getGreen() - b.getGreen())
         + std::abs (a.getBlue() - b.getBlue());
}

bool nearColour (juce::Colour actual, juce::Colour expected, int tolerance = 45) noexcept
{
    return rgbDistance (actual, expected) <= tolerance;
}

bool nearBlend (juce::Colour actual, juce::Colour fg, juce::Colour bg,
                float alpha, int tolerance = 60) noexcept
{
    return nearColour (actual, ui::dopamine::blendOver (fg, bg, alpha), tolerance);
}

juce::Colour spectrumWell() noexcept
{
    return ui::dopamine::sectionWell (ui::dopamine::Section::spectrum);
}

bool isBandEnvelopePixel (juce::Colour c) noexcept
{
    const auto well = spectrumWell();
    for (int band = 0; band < 3; ++band)
    {
        const auto accent = ui::dopamine::bandAccent (band);
        if (nearBlend (c, accent, well, 0.85f, 60)
            || nearBlend (c, accent, well, 0.45f, 60)
            || nearColour (c, accent, 40))
            return true;
    }
    return false;
}

bool isCursorPixel (juce::Colour c) noexcept
{
    const auto freq = ui::dopamine::accent (ui::dopamine::Role::frequency).base;
    return nearBlend (c, freq, spectrumWell(), 0.95f, 60)
        || nearColour (c, freq, 45);
}

bool isSpectrumPixel (juce::Colour c) noexcept
{
    const auto global = ui::dopamine::accent (ui::dopamine::Role::global).base;
    return nearBlend (c, global, spectrumWell(), 0.90f, 70)
        || nearBlend (c, global, spectrumWell(), 0.16f, 30);
}

// findChildWithID only searches direct children — recurse for the editor
// tree (editor -> band card -> slider).
juce::Component* findDeep (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* hit = findDeep (*child, id))
            return hit;
    return nullptr;
}

// T41: the zones now live inside PluginEditor::Content (the scaled canvas),
// so type lookups must recurse as well.
template <typename T>
T* findDeepType (juce::Component& root)
{
    if (auto* hit = dynamic_cast<T*> (&root))
        return hit;
    for (auto* child : root.getChildren())
        if (auto* hit = findDeepType<T> (*child))
            return hit;
    return nullptr;
}



// T19 acceptance support: renders the full editor off-screen (no window, no
// GPU — pure JUCE software painting) and checks the image is non-blank, so
// layout regressions (blank zones, collapsed zones) fail the build.
//
// When the NVD_SHOT_DIR environment variable is set, the PNG is also saved
// there (screenshot review flow), e.g.:
//   NVD_SHOT_DIR=build ./unit_tests.exe UiScreenshot
class UiScreenshotTests final : public juce::UnitTest
{
public:
    UiScreenshotTests() : juce::UnitTest ("UiScreenshot") {}

    void runTest() override
    {
        beginTest ("band-count radios track the parameter (T37)");
        {
            // Regression: getRawParameterValue returns the RAW choice INDEX
            // (0/1/2), not the normalised value — a load()*2 mapping showed
            // 2 bands as 3 bands. The radios must follow the parameter.
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            ui::CrossoverDisplay display (processor.apvts, processor.getSpectrumSource(), processor.getSpectrumSourceOut());
            display.setSize (720, 240);
            display.setVisible (true);

            juce::Image img (juce::Image::RGB, 720, 240, true, juce::SoftwareImageType());

            for (int idx = 0; idx < 3; ++idx)
            {
                processor.apvts.getParameter (params::IDs::bandCount)->setValueNotifyingHost (
                    static_cast<float> (idx) / 2.0f);
                // Force a paint: syncBandRadios runs there (timer may be
                // starved headless).
                juce::Graphics g (img);
                display.paintEntireComponent (g, true);

                int selected = -1;
                for (int i = 0; i < 3; ++i)
                {
                    auto* radio = dynamic_cast<juce::ToggleButton*> (
                        display.findChildWithID ("band_radio_" + juce::String (i)));
                    expect (radio != nullptr, "band radio missing");
                    if (radio != nullptr && radio->getToggleState())
                        selected = i;
                }
                expectEquals (selected, idx,
                              juce::String ("band radio selection for parameter index ")
                                  + juce::String (idx));
            }
        }

        beginTest ("slope envelope paints on the crossover display");
        {
            // Direct render of the CrossoverDisplay alone: the envelope must
            // be visible at the 0 dB passband line. T37: default is 1 band
            // (no lines -> no envelope) — switch to 3 bands explicitly.
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);
            processor.apvts.getParameter (params::IDs::bandCount)->setValueNotifyingHost (1.0f);

            ui::CrossoverDisplay display (processor.apvts, processor.getSpectrumSource(), processor.getSpectrumSourceOut());
            display.setSize (720, 240);
            display.setVisible (true);

            juce::Image img (juce::Image::RGB, 720, 240, true, juce::SoftwareImageType());
            juce::Graphics g (img);
            display.paintEntireComponent (g, true);

            // T62/T70: derive the well rectangle from the display itself; the
            // envelope window is [-18, +18] dB, so the 0 dB passband sits at
            // the middle of the graph height. Scan the whole well and take
            // the best row.
            const auto graph = display.getGraphArea().toNearestInt();
            int accentHits = 0, bestY = 0;
            for (int y = graph.getY(); y < graph.getBottom(); ++y)
            {
                int hits = 0;
                for (int x = graph.getX(); x < graph.getRight(); ++x)
                {
                    if (isBandEnvelopePixel (img.getPixelAt (x, y)))
                        ++hits;
                }
                if (hits > accentHits) { accentHits = hits; bestY = y; }
            }
            logMessage (juce::String ("envelope passband best row y=") + juce::String (bestY)
                        + " hits=" + juce::String (accentHits));
            expect (accentHits > 100, "slope envelope not painted");
        }

        beginTest ("slope skirts cross at the crossover below passband top");
        {
            // The LR skirts must meet at the crossover (-6 dB) BELOW the
            // 0 dB passband line — a regression guard for the dbTop fix
            // (user report: lines did not appear to cross).
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);
            // T37: default is 1 band — the skirt-crossing check needs lines.
            processor.apvts.getParameter (params::IDs::bandCount)->setValueNotifyingHost (1.0f);

            ui::CrossoverDisplay display (processor.apvts, processor.getSpectrumSource(), processor.getSpectrumSourceOut());
            // 280 px tall: the UI_STYLE_GUIDE padding + taller knob row
            // shrink the well — keep enough dB span for the skirt check.
            display.setSize (720, 280);
            display.setVisible (true);

            juce::Image img (juce::Image::RGB, 720, 280, true, juce::SoftwareImageType());
            juce::Graphics g (img);
            display.paintEntireComponent (g, true);

            const auto isAccent = [] (juce::Colour c)
            {
                return isBandEnvelopePixel (c);
            };

            // 1) find the crossover vertical lines (frequency teal cursors).
            int x1 = -1, x2 = -1;
            for (int x = 0; x < 720; ++x)
            {
                if (isCursorPixel (img.getPixelAt (x, 120)))
                {
                    if (x1 < 0) x1 = x;
                    else if (x > x1 + 10) { x2 = x; break; }
                }
            }
            expect (x1 >= 0 && x2 > x1, "crossover vertical lines not found");

            if (x1 >= 0 && x2 > x1)
            {
                logMessage ("  cursor lines x1=" + juce::String (x1) + " x2=" + juce::String (x2)
                            + " px1=" + img.getPixelAt (x1, 120).toDisplayString (true)
                            + " px2=" + img.getPixelAt (x2, 120).toDisplayString (true));
                // 2) passband top: lowest envelope y in the far-left band.
                int topY = img.getHeight();
                for (int x = 20; x < 60; ++x)
                    for (int y = 0; y < img.getHeight(); ++y)
                        if (isAccent (img.getPixelAt (x, y))) { topY = juce::jmin (topY, y); break; }

                // 3) skirt y just beside the first vertical line (avoid the
                //    line itself): the envelope below the passband.
                int crossY = img.getHeight();
                for (int dx : { -5, -3, 3, 5 })
                    for (int y = topY + 2; y < img.getHeight(); ++y)
                        if (isAccent (img.getPixelAt (x1 + dx, y))) { crossY = juce::jmin (crossY, y); break; }

                logMessage (juce::String ("  passband top y=") + juce::String (topY)
                            + ", crossover skirt y=" + juce::String (crossY));
                expect (crossY - topY >= 8,
                        "skirts do not dip below the passband at the crossover");
            }
        }

        beginTest ("slope envelope paints inside the full editor");
        {
            // Same check through the editor tree (child-component paint path).
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);
            // T37: default is 1 band (no crossover lines -> no envelope);
            // the envelope check needs lines.
            processor.apvts.getParameter (params::IDs::bandCount)->setValueNotifyingHost (1.0f);

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            auto* pe = dynamic_cast<PluginEditor*> (editor.get());
            expect (pe != nullptr, "editor should be a PluginEditor");
            if (pe == nullptr)
                return;
            pe->setUiScale (1.0f, false);
            editor->setVisible (true);

            const auto bounds = editor->getLocalBounds();
            juce::Image img (juce::Image::RGB, bounds.getWidth(), bounds.getHeight(), true,
                             juce::SoftwareImageType());
            juce::Graphics g (img);
            editor->paintEntireComponent (g, true);

            // T62: derive the well from the actual component tree (the old
            // hard-coded vertical coordinates are gone with the two-column
            // layout).
            auto* display = findDeepType<ui::CrossoverDisplay> (*editor);
            expect (display != nullptr, "CrossoverDisplay not found");
            if (display == nullptr)
                return;

            const auto graph = editor->getLocalArea (
                display, display->getGraphArea().toNearestInt());
            int bestHits = 0, bestY = 0;
            for (int y = graph.getY(); y < graph.getBottom(); ++y)
            {
                int hits = 0;
                for (int x = graph.getX(); x < graph.getRight(); ++x)
                {
                    if (isBandEnvelopePixel (img.getPixelAt (x, y)))
                        ++hits;
                }
                if (hits > bestHits) { bestHits = hits; bestY = y; }
            }
            logMessage (juce::String ("editor envelope passband best row y=") + juce::String (bestY)
                        + " hits=" + juce::String (bestHits));
            // The editor is rendered at 85 % (1020x612), so the same envelope
            // produces fewer physical pixels than the 1:1 standalone test.
            expect (bestHits > 20, "slope envelope not painted inside the editor");
        }

        beginTest ("formant shift knob greys out while disabled");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            if (auto* pe = dynamic_cast<PluginEditor*> (editor.get()))
                pe->setUiScale (1.0f, false);   // T62 production default
            editor->setVisible (true);

            // T66: Growl is the default page; the Formant knobs live on Master.
            if (auto* bar = findDeepType<ui::BottomBarComponent> (*editor))
                bar->getMasterTabs().setCurrentTabIndex (1);

            auto* slider = dynamic_cast<juce::Slider*> (findDeep (*editor, params::IDs::formantShift));
            expect (slider != nullptr, "formant_shift slider not found");
            if (slider != nullptr)
            {
                // Default state: formant disabled -> the knob must be greyed.
                expect (! slider->isEnabled(), "formant knob should be disabled by default");
                logMessage ("  default: formant knob disabled (grey)");

                // Toggle on -> listener -> AsyncUpdater -> knob re-enabled.
                if (auto* p = processor.apvts.getParameter (params::IDs::formantEnabled))
                    p->setValueNotifyingHost (1.0f);
                if (auto* bar = slider->findParentComponentOfClass<ui::BottomBarComponent>())
                    bar->flushPendingAsyncUpdate();
                expect (slider->isEnabled(), "formant knob should re-enable after toggle-on");
                logMessage ("  after toggle-on: formant knob enabled");
            }
        }

        beginTest ("spectrum curve reflects a -40 dBFS input (not pinned)");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            auto* pe = dynamic_cast<PluginEditor*> (editor.get());
            expect (pe != nullptr, "editor should be a PluginEditor");
            if (pe == nullptr)
                return;
            pe->setUiScale (1.0f, false);
            editor->setVisible (true);

            // Feed ~-40 dBFS 1 kHz sine for a second so the spectrum FIFO
            // fills and the crossover display's timer paints a curve.
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            constexpr float level = 0.01f; // -40 dBFS
            for (int b = 0; b < 100; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const auto t = static_cast<float> (b * 512 + i) / 48000.0f;
                    const auto v = level * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * t);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                processor.processBlock (buf, midi);
            }

            // Fire due timers directly (headless builds have no dispatch loop),
            // then force one synchronous FFT refresh so the curve is based on
            // the audio we just pushed (the timer may not be pending here).
            juce::Timer::callPendingTimersSynchronously();

            auto* display = findDeepType<ui::CrossoverDisplay> (*editor);
            expect (display != nullptr, "CrossoverDisplay not found");
            if (display == nullptr)
                return;
            display->refreshSpectrumForTest();

            const auto bounds = editor->getLocalBounds();
            juce::Image img (juce::Image::RGB, bounds.getWidth(), bounds.getHeight(), true,
                             juce::SoftwareImageType());
            juce::Graphics g (img);
            editor->paintEntireComponent (g, true);

            // T62/T70: derive the well and the 1 kHz column from the actual
            // component bounds. With the 0..-72 dBFS spectrum window a
            // -40 dBFS peak sits about 56 % above the bottom, well below
            // the top.
            const auto graph = editor->getLocalArea (
                display, display->getGraphArea().toNearestInt());
            const int x1k = graph.getX() + juce::roundToInt (
                ui::CrossoverDisplay::freqToX (1000.0f, static_cast<float> (graph.getWidth())));

            // Find the TOPMOST accent pixel across the 1 kHz window. The
            // spectrum fill also touches the graph bottom, so scanning must
            // not stop at the first x column.
            int curveTop = graph.getBottom();
            for (int x = x1k - 60; x <= x1k + 60; ++x)
            {
                for (int y = graph.getY() + 18; y < graph.getBottom() - 2; ++y)
                {
                    if (isSpectrumPixel (img.getPixelAt (x, y)))
                    {
                        curveTop = juce::jmin (curveTop, y);
                        break;
                    }
                }
            }
            logMessage (juce::String ("  1 kHz curve top y=") + juce::String (curveTop)
                        + " (top=" + juce::String (graph.getY())
                        + ", bottom=" + juce::String (graph.getBottom()) + ")");
            expect (curveTop > graph.getY() + 10, "spectrum pinned at the top for -40 dBFS input");
            expect (curveTop < graph.getBottom() - 20, "spectrum curve not found in the well");
        }

        beginTest ("editor installs the theme as global default L&F (font path)");
        {
            // JUCE resolves typefaces through the GLOBAL default L&F, so the
            // embedded Victor Mono only renders when a NeoLookAndFeel is the
            // default — regression guard for the DAW smoke finding.
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
                auto& def = juce::LookAndFeel::getDefaultLookAndFeel();
                expect (dynamic_cast<ui::NeoLookAndFeel*> (&def) != nullptr,
                        "editor did not install the theme as global default L&F");
                logMessage (juce::String ("  global default L&F: ")
                            + (dynamic_cast<ui::NeoLookAndFeel*> (&def) != nullptr
                               ? "NeoLookAndFeel" : "other"));
            }
            // Editor closed: the default must be restored (not dangling).
            expect (dynamic_cast<ui::NeoLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()) == nullptr,
                    "default L&F still points at the closed editor's theme");
        }

        beginTest ("band_count automation defers GUI updates (S1)");
        {
            // APVTS listeners fire on the parameter-writer thread — host
            // automation can write on the audio thread, so the GUI update
            // must be deferred (AsyncUpdater), never executed inline.
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            // T37: the layout default is 1 band — this test needs 3 bands to
            // observe the grey-out transition, so switch explicitly first.
            processor.apvts.getParameter (params::IDs::bandCount)->setValueNotifyingHost (1.0f);

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            if (auto* pe = dynamic_cast<PluginEditor*> (editor.get()))
                pe->setUiScale (1.0f, false);   // T62 production default
            editor->setVisible (true);

            // T38: the cards live in tabs — the tab bar keeps only the
            // CURRENT tab's card in the child tree, so switch to tab 3 first.
            if (auto* tabs = dynamic_cast<juce::TabbedComponent*> (findDeep (*editor, "bandTabs")))
                tabs->setCurrentTabIndex (2);

            auto* slider3 = dynamic_cast<juce::Slider*> (
                findDeep (*editor, params::makeBandParameterID (3, params::IDs::jitterDepth)));
            expect (slider3 != nullptr && slider3->isEnabled(),
                    "band 3 card should start enabled (3 bands)");

            // Simulate an audio-thread automation write (no message loop
            // here, so the deferred update must NOT have run yet).
            processor.apvts.getParameter (params::IDs::bandCount)->setValueNotifyingHost (0.0f);
            expect (slider3->isEnabled(),
                    "GUI must not be touched from the parameter callback (S1)");

            // Flush the pending async update (message-thread equivalent).
            auto* pluginEditor = dynamic_cast<PluginEditor*> (editor.get());
            expect (pluginEditor != nullptr, "editor should be a PluginEditor");
            pluginEditor->flushPendingAsyncUpdate();

            // T38: band_count=1 rebuilds the tabs — tab 3 no longer exists,
            // so the b3 controls leave the visible tree.
            auto* tabs = dynamic_cast<juce::TabbedComponent*> (findDeep (*editor, "bandTabs"));
            expect (tabs != nullptr && tabs->getNumTabs() == 1,
                    "after the deferred update only the Band 1 tab must remain");
            expect (findDeep (*editor, params::makeBandParameterID (3, params::IDs::jitterDepth)) == nullptr,
                    "after the deferred update band 3 controls must leave the tree");
        }

        beginTest ("two processor instances keep their meters separate (M1)");
        {
            // Regression: the old static tap bridge let the last-prepared
            // instance steal every meter's peaks.
            PluginProcessor a, b;
            a.prepareToPlay (48000.0, 512);
            b.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> loud (2, 512), quiet (2, 512);
            loud.clear(); quiet.clear();
            for (int i = 0; i < 512; ++i)
            {
                loud.setSample (0, i, 0.5f);
                loud.setSample (1, i, 0.4f);
                quiet.setSample (0, i, 0.02f);
                quiet.setSample (1, i, 0.01f);
            }

            juce::MidiBuffer midi;
            a.processBlock (loud, midi);
            b.processBlock (quiet, midi);

            float l = 0.0f, r = 0.0f;
            expect (a.getMeterSource().readLatest (l, r), "A should have meter data");
            expect (l > 0.4f, juce::String ("A meter got ") + juce::String (l)
                                  + " instead of its own loud peaks");
            expect (b.getMeterSource().readLatest (l, r), "B should have meter data");
            expect (l < 0.1f, juce::String ("B meter got ") + juce::String (l)
                                  + " — cross-instance stealing (M1)");
        }

        beginTest ("spectrum decimation reports the effective rate (O1)");
        {
            ui::SpectrumSource src;
            src.prepare (44100.0);
            expectWithinAbsoluteError (src.getEffectiveSampleRate(), 44100.0, 0.01,
                                       "44.1k decimates by 1 -> 44100");
            src.prepare (48000.0);
            expectWithinAbsoluteError (src.getEffectiveSampleRate(), 48000.0, 0.01,
                                       "48k -> 48000");
            src.prepare (88200.0);
            expectWithinAbsoluteError (src.getEffectiveSampleRate(), 44100.0, 0.01,
                                       "88.2k decimates by 2 -> 44100");
            src.prepare (192000.0);
            expectWithinAbsoluteError (src.getEffectiveSampleRate(), 48000.0, 0.01,
                                       "192k decimates by 4 -> 48000");
        }

        beginTest ("UI scale: default 100% is the 1200x720 two-column canvas (T41/T63)");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            expect (editor != nullptr, "editor creation failed");
            if (editor == nullptr)
                return;

            auto* pe = dynamic_cast<PluginEditor*> (editor.get());
            expect (pe != nullptr, "editor should be a PluginEditor");
            if (pe == nullptr)
                return;

            // A previously saved user scale must not make the default-size
            // assertion order-dependent: reset to 100 % in-memory first.
            pe->setUiScale (1.0f, false);

            const auto defaultSize = PluginEditor::sizeForUserScale (1.0f);
            expectEquals (defaultSize.x, 1020, "default 100% width (1200 * 0.85)");
            expectEquals (defaultSize.y, 612, "default 100% height (720 * 0.85)");
            expectEquals (editor->getWidth(), defaultSize.x);
            expectEquals (editor->getHeight(), defaultSize.y);
            logMessage ("  default UI size: " + juce::String (defaultSize.x) + " x "
                        + juce::String (defaultSize.y));

            pe->setUiScale (1.25f, false);
            expectWithinAbsoluteError (pe->getUserScale(), 1.25f, 1e-5f);
            const auto size125 = PluginEditor::sizeForUserScale (1.25f);
            expectEquals (editor->getWidth(), size125.x);
            expectEquals (editor->getHeight(), size125.y);
            expectGreaterThan (size125.x, defaultSize.x, "125% must be larger than 100%");

            pe->setUiScale (0.80f, false);
            expectLessThan (PluginEditor::sizeForUserScale (0.80f).x, defaultSize.x,
                            "80% must be smaller than 100%");

            // Clamped to [minUserScale, maxUserScale].
            pe->setUiScale (9.0f, false);
            expectWithinAbsoluteError (pe->getUserScale(), PluginEditor::maxUserScale, 1e-5f);
            pe->setUiScale (0.10f, false);
            expectWithinAbsoluteError (pe->getUserScale(), PluginEditor::minUserScale, 1e-5f);
        }

        beginTest ("editor renders a non-blank four-zone layout");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);
            // Three tabs make the tab strip styling reviewable in the shot
            // (default band_count=1 shows a single tab).
            processor.apvts.getParameter (params::IDs::bandCount)->setValueNotifyingHost (1.0f);

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            expect (editor != nullptr, "editor creation failed");
            if (editor == nullptr)
                return;

            // T41: render at the production default (100 %), independent of
            // any saved user scale.
            if (auto* pe = dynamic_cast<PluginEditor*> (editor.get()))
                pe->setUiScale (1.0f, false);
            editor->setVisible (true); // paintEntireComponent skips invisible roots
            const auto bounds = editor->getLocalBounds();

            logMessage (juce::String ("editor visible=") + juce::String (editor->isVisible() ? "y" : "n")
                        + " onDesktop=" + juce::String (editor->isOnDesktop() ? "y" : "n"));

            // SoftwareImageType: NativeImageType (the default) needs a desktop
            // surface, which headless test runs do not have.
            juce::Image image (juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true,
                               juce::SoftwareImageType());

            // sanity: raw Graphics writes into the image?
            {
                juce::Graphics g0 (image);
                g0.fillAll (juce::Colours::red);
                logMessage (juce::String ("raw fill centre=")
                            + image.getPixelAt (360, 280).toDisplayString (true));
                g0.fillAll (juce::Colours::transparentBlack);
            }

            juce::Graphics g (image);
            editor->paintEntireComponent (g, true);

            // ---- non-blank check ----
            // Sample a grid of pixels: count distinct luminance buckets.
            std::array<int, 8> buckets {};
            const auto step = 6; // every 6th pixel
            for (int y = 0; y < bounds.getHeight(); y += step)
            {
                for (int x = 0; x < bounds.getWidth(); x += step)
                {
                    const auto c = image.getPixelAt (x, y);
                    const auto lum = (c.getRed() + c.getGreen() + c.getBlue()) / 3;
                    buckets[static_cast<size_t> (juce::jlimit (0, 7, lum * 8 / 256))]++;
                }
            }
            int populated = 0;
            for (int b : buckets)
                if (b > 0)
                    ++populated;
            expect (populated >= 3,
                    juce::String ("image looks blank/flat: ") + juce::String (populated)
                        + " luminance buckets populated");

            // ---- zone sanity: header + left column + right tabs + Master ----
            // T62: derive each zone from its actual component bounds instead
            // of the old vertical stack coordinates.
            const auto background = image.getPixelAt (2, 2); // corner = surface fill
            logMessage (juce::String ("bg px=") + background.toDisplayString (true)
                        + " centre=" + image.getPixelAt (360, 280).toDisplayString (true));

            const auto probeZone = [&] (juce::Component* zone, const char* label)
            {
                expect (zone != nullptr, juce::String (label) + " not found");
                if (zone == nullptr)
                    return;
                const auto r = editor->getLocalArea (zone, zone->getLocalBounds());
                const int y = r.getCentreY();
                int nonBg = 0;
                for (int x = r.getX(); x < r.getRight(); x += 3)
                    if (image.getPixelAt (x, y) != background)
                        ++nonBg;
                expect (nonBg > 10,
                        juce::String (label) + " looks empty ("
                            + juce::String (nonBg) + " non-bg px)");
            };

            probeZone (findDeepType<ui::TopBarComponent> (*editor), "header");
            probeZone (findDeepType<ui::CrossoverDisplay> (*editor), "left spectrum column");
            probeZone (findDeep (*editor, "bandTabs"), "right band tabs");
            probeZone (findDeepType<ui::BottomBarComponent> (*editor), "Master panel");

            // ---- optional BMP export for screenshot review ----
            if (const char* dir = std::getenv ("NVD_SHOT_DIR"))
            {
                const auto dirFile = juce::File (juce::String (dir));

                // NOTE: PNGImageFormat writes ALL-BLACK output in headless
                // runs (JUCE 9, verified), so we export a hand-rolled 24-bit
                // BMP instead (few bytes of header + BGR rows).
                const auto saveBmp = [this, &dirFile] (const juce::Image& source,
                                                       const juce::String& fileName)
                {
                    const int w = source.getWidth(), h = source.getHeight();
                    juce::Image rgb (juce::Image::RGB, w, h, true,
                                     juce::SoftwareImageType());
                    juce::Graphics gr (rgb);
                    gr.drawImageAt (source, 0, 0);

                    const int rowSize = (w * 3 + 3) & ~3;
                    const int dataSize = rowSize * h;
                    juce::MemoryBlock bmp (static_cast<size_t> (54 + dataSize));
                    auto* d = static_cast<uint8_t*> (bmp.getData());
                    d[0] = 'B'; d[1] = 'M';
                    const auto put32 = [d] (int off, uint32_t v)
                    {
                        d[static_cast<size_t> (off)] = static_cast<uint8_t> (v & 0xFF);
                        d[static_cast<size_t> (off + 1)] = static_cast<uint8_t> ((v >> 8) & 0xFF);
                        d[static_cast<size_t> (off + 2)] = static_cast<uint8_t> ((v >> 16) & 0xFF);
                        d[static_cast<size_t> (off + 3)] = static_cast<uint8_t> ((v >> 24) & 0xFF);
                    };
                    put32 (2, static_cast<uint32_t> (54 + dataSize));
                    put32 (10, 54);
                    put32 (14, 40);
                    put32 (18, static_cast<uint32_t> (w));
                    put32 (22, static_cast<uint32_t> (h));
                    put32 (26, 1);   // planes
                    put32 (28, 24);  // bpp
                    put32 (30, 0);   // BI_RGB (no compression)
                    put32 (34, static_cast<uint32_t> (dataSize));
                    put32 (38, 2835); // 72 dpi
                    put32 (42, 2835);
                    put32 (46, 0);   // palette colours
                    put32 (50, 0);

                    for (int y = 0; y < h; ++y)
                    {
                        auto* row = d + 54 + static_cast<size_t> (h - 1 - y) * rowSize;
                        for (int x = 0; x < w; ++x)
                        {
                            const auto c = rgb.getPixelAt (x, y);
                            row[x * 3] = c.getBlue();
                            row[x * 3 + 1] = c.getGreen();
                            row[x * 3 + 2] = c.getRed();
                        }
                    }
                    // JUCE 9 FileOutputStream OPEN_ALWAYS + FILE_END appends —
                    // delete first so the file holds exactly this render.
                    const auto path = dirFile.getChildFile (fileName);
                    path.deleteFile();
                    juce::FileOutputStream bmpOut (path);
                    bmpOut.write (bmp.getData(), bmp.getSize());
                    bmpOut.flush();
                    logMessage ("screenshot saved: " + path.getFullPathName());
                };

                // 100 % first (the image already rendered above), then the
                // extreme UI sizes for clipping review.
                saveBmp (image, "ui_editor.bmp");

                // T66: capture the Master page too (the default page is Growl).
                if (auto* bar = findDeepType<ui::BottomBarComponent> (*editor))
                {
                    bar->getMasterTabs().setCurrentTabIndex (1);
                    juce::Image masterImg (juce::Image::ARGB, editor->getWidth(),
                                           editor->getHeight(), true,
                                           juce::SoftwareImageType());
                    juce::Graphics mg (masterImg);
                    editor->paintEntireComponent (mg, true);
                    saveBmp (masterImg, "ui_editor_master.bmp");
                    bar->getMasterTabs().setCurrentTabIndex (0);
                }

                if (auto* pe = dynamic_cast<PluginEditor*> (editor.get()))
                {
                    for (const auto& shot : { std::pair<float, const char*> { 0.75f, "ui_editor_75.bmp" },
                                              std::pair<float, const char*> { 1.25f, "ui_editor_125.bmp" },
                                              std::pair<float, const char*> { 1.50f, "ui_editor_150.bmp" } })
                    {
                        pe->setUiScale (shot.first, false);
                        const auto scaledBounds = editor->getLocalBounds();
                        juce::Image scaled (juce::Image::ARGB, scaledBounds.getWidth(),
                                            scaledBounds.getHeight(), true,
                                            juce::SoftwareImageType());
                        juce::Graphics sg (scaled);
                        editor->paintEntireComponent (sg, true);
                        saveBmp (scaled, shot.second);
                    }
                }

                // T64: popup menu review (text side padding + contrast
                // against the plugin canvas).
                {
                    juce::Image menuImg (juce::Image::ARGB, 320, 330, true,
                                         juce::SoftwareImageType());
                    juce::Graphics mg (menuImg);
                    mg.fillAll (ui::NeoLookAndFeel::Tokens::background());
                    auto& lnf = editor->getLookAndFeel();
                    lnf.drawPopupMenuBackground (mg, 320, 330);

                    // JUCE lays menu items across the full window width (the
                    // border is content padding, not an item inset).
                    constexpr int itemH = 30, itemX = 0, itemW = 320, itemTop = 14;
                    for (int i = 0; i < 6; ++i)
                        lnf.drawPopupMenuItem (mg,
                                               { itemX, itemTop + i * itemH, itemW, itemH },
                                               false, true, i == 1, i == 2, i == 4,
                                               "Factory Preset " + juce::String (i + 1),
                                               i == 4 ? "Ctrl+I" : "", nullptr, nullptr);

                    // T65 follow-up (user: add a drop shadow to the dropdown
                    // menu): the 14 px border must show a projection outside
                    // the face, not bare canvas.
                    const auto canvasColour = ui::NeoLookAndFeel::Tokens::background();
                    const auto shadowPixel = menuImg.getPixelAt (160, 322);
                    expectLessThan (shadowPixel.getBrightness(),
                                    canvasColour.getBrightness() - 0.03f,
                                    "popup menu needs a visible drop shadow");

                    saveBmp (menuImg, "ui_popup.bmp");
                }
            }
        }
    }
};

UiScreenshotTests uiScreenshotTests;

} // namespace
