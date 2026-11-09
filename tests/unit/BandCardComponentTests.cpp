#include <juce_audio_utils/juce_audio_utils.h>

#include <vector>

#include "PluginProcessor.h"
#include "ui/BandCardComponent.h"
#include "ui/NeoLookAndFeel.h"

namespace
{

// T18/T62: parameter-linkage checks for the band cards. The Sideband UI is
// hidden (T62) but its parameters remain in the APVTS, so the tests assert
// both the visible bindings and the hidden-parameter contract.
class BandCardComponentTests final : public juce::UnitTest
{
public:
    BandCardComponentTests() : juce::UnitTest ("BandCardComponent") {}

    void runTest() override
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        beginTest ("visible per-band controls bind; hidden Sideband controls are gone");
        {
            ui::BandCardComponent card (processor.apvts, 1);
            card.setSize (400, 400);

            // T63: 8 controls — 7 knobs + vib combo. The Band On switch moved
            // to the tab strip, so the card must NOT own `b1_enable`.
            const char* bases[] =
            {
                params::IDs::jitterDepth, params::IDs::jitterRate,
                params::IDs::shimmerDepth, params::IDs::shimmerRate,
                params::IDs::fog,
                params::IDs::vibRate, params::IDs::vibDepth, params::IDs::vibWave
            };

            for (const char* base : bases)
            {
                const auto full = params::makeBandParameterID (1, base);
                expect (card.findChildWithID (full) != nullptr,
                        "missing control for " + full);
            }

            const auto enableId = params::makeBandParameterID (1, params::IDs::enable);
            expect (card.findChildWithID (enableId) == nullptr,
                    "Band On must leave the card and live in the tab strip");
            expect (processor.apvts.getParameter (enableId) != nullptr,
                    "Band On parameter must stay in the APVTS");

            // T62: the Sideband section left the card, but every parameter
            // must stay in the APVTS for presets / automation.
            for (const char* base :
                 { params::IDs::sidebandEnable, params::IDs::sidebandDepth,
                   params::IDs::sidebandFmRatio, params::IDs::sidebandRoughness,
                   params::IDs::sidebandLowcut, params::IDs::sidebandBright })
            {
                const auto full = params::makeBandParameterID (1, base);
                expect (card.findChildWithID (full) == nullptr,
                        "Sideband control " + full + " should have left the card");
                expect (processor.apvts.getParameter (full) != nullptr,
                        "Sideband parameter " + full + " must stay in the APVTS");
            }

            // Only the whole-band Rough remains in the card.
            int roughLabels = 0;
            for (int i = 0; i < card.getNumChildComponents(); ++i)
                if (auto* label = dynamic_cast<juce::Label*> (card.getChildComponent (i)))
                    if (label->getText() == "Rough")
                        ++roughLabels;
            expectEquals (roughLabels, 1, "exactly one Rough label expected");

            // The legacy harmonic controls are GONE from the card (but the
            // parameters themselves remain in the APVTS — §6.2).
            for (const char* base : { params::IDs::harmonicDrive, params::IDs::harmonicTone })
            {
                const auto full = params::makeBandParameterID (1, base);
                expect (card.findChildWithID (full) == nullptr,
                        "legacy control " + full + " should have left the card");
                expect (processor.apvts.getParameter (full) != nullptr,
                        "legacy parameter " + full + " must stay in the APVTS");
            }
        }

        beginTest ("first section is titled Band Effects (T49) and Fog binds bidirectionally");
        {
            ui::BandCardComponent card (processor.apvts, 1);

            bool foundTitle = false;
            for (int i = 0; i < card.getNumChildComponents(); ++i)
                if (auto* label = dynamic_cast<juce::Label*> (card.getChildComponent (i)))
                    if (label->getText() == "Band Effects")
                        foundTitle = true;
            expect (foundTitle, "Band Effects section title missing");

            const auto full = params::makeBandParameterID (1, params::IDs::fog);
            auto* param = processor.apvts.getParameter (full);
            auto* slider = dynamic_cast<juce::Slider*> (card.findChildWithID (full));
            expect (param != nullptr && slider != nullptr, "b1 fog control missing");
            if (param == nullptr || slider == nullptr)
                return;

            param->setValueNotifyingHost (0.75f);
            expectWithinAbsoluteError (slider->getValue(), 75.0, 0.001,
                                       "apvts -> fog slider sync");

            slider->setValue (20.0, juce::sendNotificationSync);
            expectWithinAbsoluteError (static_cast<double> (param->getValue()), 0.2, 0.001,
                                       "fog slider -> apvts sync");
        }

        beginTest ("controls are laid out in aligned 3-column cells inside the card");
        {
            ui::BandCardComponent card (processor.apvts, 1);
            card.setSize (406, 400);   // T63: Band card keeps its height

            const auto getSlider = [&card] (juce::StringRef base)
            {
                return dynamic_cast<juce::Slider*> (card.findChildWithID (
                    params::makeBandParameterID (1, base)));
            };

            const char* sliderBases[] =
            {
                params::IDs::jitterDepth, params::IDs::jitterRate,
                params::IDs::shimmerDepth, params::IDs::shimmerRate,
                params::IDs::fog, params::IDs::vibDepth, params::IDs::vibRate
            };

            std::vector<juce::Rectangle<int>> sliderBounds;
            for (const char* base : sliderBases)
            {
                auto* slider = getSlider (base);
                expect (slider != nullptr, juce::String ("missing slider ") + base);
                if (slider == nullptr)
                    continue;

                const auto b = slider->getBoundsInParent();
                expect (card.getLocalBounds().contains (b),
                        juce::String (base) + " escapes the card bounds: " + b.toString());
                expect (b.getWidth() > 0 && b.getHeight() > 0,
                        juce::String (base) + " has zero size (layout collapsed)");
                sliderBounds.push_back (b);
            }

            // The 7 sliders must not overlap each other.
            for (size_t i = 0; i < sliderBounds.size(); ++i)
                for (size_t j = i + 1; j < sliderBounds.size(); ++j)
                    expect (! sliderBounds[i].intersects (sliderBounds[j]),
                            "sliders overlap: " + sliderBounds[i].toString()
                                + " vs " + sliderBounds[j].toString());

            auto* jitDepth = getSlider (params::IDs::jitterDepth);
            auto* jitRate  = getSlider (params::IDs::jitterRate);
            auto* fog      = getSlider (params::IDs::fog);
            auto* shimDepth = getSlider (params::IDs::shimmerDepth);
            auto* shimRate  = getSlider (params::IDs::shimmerRate);
            auto* vibDepth  = getSlider (params::IDs::vibDepth);
            auto* vibRate   = getSlider (params::IDs::vibRate);
            auto* wave = card.findChildWithID (
                params::makeBandParameterID (1, params::IDs::vibWave));

            expect (jitDepth != nullptr && jitRate != nullptr && fog != nullptr
                        && shimDepth != nullptr && shimRate != nullptr
                        && vibDepth != nullptr && vibRate != nullptr && wave != nullptr,
                    "layout diagnostics missing a control");
            if (jitDepth == nullptr || jitRate == nullptr || fog == nullptr
                || shimDepth == nullptr || shimRate == nullptr
                || vibDepth == nullptr || vibRate == nullptr || wave == nullptr)
                return;

            // Row 1: Jit Depth / Jit Rate / Rough on one line.
            expectEquals (jitDepth->getY(), jitRate->getY(), "row 1 y");
            expectEquals (jitDepth->getY(), fog->getY(), "row 1 y");
            expectLessThan (jitDepth->getX(), jitRate->getX(), "row 1 x order");
            expectLessThan (jitRate->getX(), fog->getX(), "row 1 x order");

            // Row 2 stays in columns 1/2 and sits below row 1.
            expectEquals (shimDepth->getX(), jitDepth->getX(), "Shim Depth column");
            expectEquals (shimRate->getX(), jitRate->getX(), "Shim Rate column");
            expectGreaterThan (shimDepth->getY(), jitDepth->getY(), "Shim row below Jit row");

            // Vibration Depth/Rate use the same columns below the wave combo.
            expectEquals (vibDepth->getX(), jitDepth->getX(), "Vib Depth column");
            expectEquals (vibRate->getX(), jitRate->getX(), "Vib Rate column");
            expectGreaterThan (vibDepth->getY(), shimDepth->getY(), "Vib row below Shim row");

            // T63 follow-up: Wave aligns to the visible knob faces — its left
            // edge on the Jit Depth disc, its right edge on the Rough disc.
            const auto waveBounds = wave->getBoundsInParent();
            const auto jitDisc = ui::NeoLookAndFeel::getKnobDiscBounds (*jitDepth);
            const auto roughDisc = ui::NeoLookAndFeel::getKnobDiscBounds (*fog);
            expectEquals (waveBounds.getX(), juce::roundToInt (jitDisc.getX()),
                          "wave combo left edge must align with the Jit Depth knob");
            expectEquals (waveBounds.getRight(), juce::roundToInt (roughDisc.getRight()),
                          "wave combo right edge must align with the Rough knob");
            expectGreaterOrEqual (waveBounds.getHeight(), 36,
                                  "wave combo too short");
            expect (! waveBounds.intersects (vibDepth->getBoundsInParent())
                        && ! waveBounds.intersects (vibRate->getBoundsInParent()),
                    "wave combo overlaps the Vibration knobs");
            for (const auto& b : sliderBounds)
                expect (! waveBounds.intersects (b),
                        "wave combo overlaps a knob: " + b.toString());

            juce::Label* waveLabel = nullptr;
            for (int i = 0; i < card.getNumChildComponents(); ++i)
                if (auto* label = dynamic_cast<juce::Label*> (card.getChildComponent (i)))
                    if (label->getText() == "Wave")
                        waveLabel = label;
            expect (waveLabel != nullptr, "Wave label missing");
            if (waveLabel != nullptr)
            {
                expectLessOrEqual (waveLabel->getBottom(), waveBounds.getY(),
                                   "Wave label must sit above the combo");
                expectWithinAbsoluteError (static_cast<double> (waveLabel->getBoundsInParent().getCentreX()),
                                           static_cast<double> (waveBounds.getCentreX()),
                                           1.0, "Wave label must be centred over the combo");
            }
        }

        beginTest ("slider attachment is two-way synced");
        {
            ui::BandCardComponent card (processor.apvts, 2);
            card.setSize (400, 400);

            const auto full = params::makeBandParameterID (2, params::IDs::jitterDepth);
            auto* param = processor.apvts.getParameter (full);
            auto* slider = dynamic_cast<juce::Slider*> (card.findChildWithID (full));
            expect (param != nullptr && slider != nullptr, "b2 jitter_depth control missing");
            if (param == nullptr || slider == nullptr)
                return;

            // APVTS -> slider. Slider values are PHYSICAL: jitter_depth is
            // a linear 0..1200 ct range, so norm 0.5 -> 600 ct.
            param->setValueNotifyingHost (0.5f);
            expectWithinAbsoluteError (slider->getValue(), 600.0, 0.001,
                                       "apvts -> slider sync");

            // slider -> APVTS: 12.5 ct -> norm 12.5/1200.
            slider->setValue (12.5, juce::sendNotificationSync);
            expectWithinAbsoluteError (static_cast<double> (param->getValue()), 12.5 / 1200.0, 0.001,
                                       "slider -> apvts sync");
        }

        beginTest ("grey-out keeps parameter values (R12 / T38 band switch)");
        {
            ui::BandCardComponent card (processor.apvts, 3);
            card.setSize (400, 400);

            const auto full = params::makeBandParameterID (3, params::IDs::fog);
            auto* param = processor.apvts.getParameter (full);
            auto* slider = dynamic_cast<juce::Slider*> (card.findChildWithID (full));
            expect (param != nullptr && slider != nullptr, "b3 fog control missing");
            if (param == nullptr || slider == nullptr)
                return;

            const auto repaint = [&card]
            {
                juce::Image img (juce::Image::RGB, 400, 400, true, juce::SoftwareImageType());
                juce::Graphics g (img);
                card.paintEntireComponent (g, true);
            };

            auto* enableParam = processor.apvts.getParameter (
                params::makeBandParameterID (3, params::IDs::enable));
            expect (enableParam != nullptr, "b3_enable parameter missing");
            if (enableParam == nullptr)
                return;

            // Batch-1 (UI audit): the painted section rows and their child
            // titles follow the band state as well.
            const auto countSectionTitles = [&card] (bool wantEnabled)
            {
                int n = 0;
                for (int i = 0; i < card.getNumChildComponents(); ++i)
                    if (auto* label = dynamic_cast<juce::Label*> (card.getChildComponent (i)))
                        if (label->getText() == "Band Effects" || label->getText() == "Vibration")
                            if (label->isEnabled() == wantEnabled)
                                ++n;
                return n;
            };

            enableParam->setValueNotifyingHost (0.0f);
            repaint();
            expect (! slider->isEnabled(), "slider should be disabled when the band is off");
            expectEquals (countSectionTitles (false), 2,
                          "section titles must dim together with the band");

            // Writing the parameter while greyed out must still work and the
            // stored value must survive (R12: parameters are kept, not reset).
            param->setValueNotifyingHost (0.7f);
            expectWithinAbsoluteError (static_cast<double> (param->getValue()), 0.7, 0.001,
                                       "stored value kept while greyed out");
            // fog is 0..100 %, so norm 0.7 -> 70.
            expectWithinAbsoluteError (slider->getValue(), 70.0, 0.001,
                                       "slider follows the value while greyed out");

            enableParam->setValueNotifyingHost (1.0f);
            repaint();
            expect (slider->isEnabled(), "slider re-enabled after band on");
            expectEquals (countSectionTitles (true), 2,
                          "section titles must restore when the band is back on");
        }

        beginTest ("vib waveform combo binds bidirectionally");
        {
            ui::BandCardComponent card (processor.apvts, 1);
            card.setSize (400, 400);

            const auto full = params::makeBandParameterID (1, params::IDs::vibWave);
            auto* param = processor.apvts.getParameter (full);
            auto* combo = dynamic_cast<juce::ComboBox*> (card.findChildWithID (full));
            expect (param != nullptr && combo != nullptr, "b1 vib_wave control missing");
            if (param == nullptr || combo == nullptr)
                return;

            // Choice with 4 entries: index 1 (Square) == 1/3 normalized.
            param->setValueNotifyingHost (1.0f / 3.0f);
            expectEquals (combo->getSelectedItemIndex(), 1, "apvts -> combo sync");

            // combo -> APVTS.
            combo->setSelectedItemIndex (2, juce::sendNotificationSync);
            expectWithinAbsoluteError (static_cast<double> (param->getValue()), 2.0 / 3.0, 0.01,
                                       "combo -> apvts sync");
        }

        beginTest ("sideband_enable parameter survives without UI (T62)");
        {
            const auto full = params::makeBandParameterID (1, params::IDs::sidebandEnable);
            auto* param = processor.apvts.getParameter (full);
            expect (param != nullptr, "b1 sideband_enable parameter missing");
            if (param != nullptr)
                expectGreaterThan (static_cast<double> (param->getValue()), 0.5,
                                   "sideband_enable should keep its default-on value");
        }
    }
};

BandCardComponentTests bandCardComponentTests;

} // namespace
