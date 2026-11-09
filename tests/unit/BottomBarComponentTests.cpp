#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/BottomBarComponent.h"

namespace
{

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

juce::Component* findDeepID (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;
    for (auto* child : root.getChildren())
        if (auto* hit = findDeepID (*child, id))
            return hit;
    return nullptr;
}

// T19/T66: Master card parameter linkage and the Growl/Master tab pages.
class BottomBarComponentTests final : public juce::UnitTest
{
public:
    BottomBarComponentTests() : juce::UnitTest ("BottomBarComponent") {}

    void runTest() override
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 512);

        beginTest ("Growl is the default tab; both pages bind their parameters");
        {
            ui::BottomBarComponent bar (processor.apvts, processor.getMeterSource(), processor);
            bar.setSize (406, 188);

            auto& tabs = bar.getMasterTabs();
            expectEquals (tabs.getNumTabs(), 2, "Growl + Master tabs");
            expectEquals (tabs.getCurrentTabIndex(), 0, "Growl must be the default page");
            expectEquals (tabs.getTabBarDepth(), 30, "Band-style 30 px tab bar");
            expect (tabs.getTabbedButtonBar().getTabButton (0)->getButtonText() == "Growl",
                    "left tab must be Growl");
            expect (tabs.getTabbedButtonBar().getTabButton (1)->getButtonText() == "Master",
                    "right tab must be Master");

            for (const char* id : { params::IDs::growlAmount, params::IDs::growlRate,
                                    params::IDs::growlTone })
                expect (findDeepID (bar, id) != nullptr,
                        "missing Growl control for " + juce::String (id));

            tabs.setCurrentTabIndex (1);
            for (const char* id : { params::IDs::formantShift, params::IDs::dryWet,
                                    params::IDs::outputTrim, params::IDs::formantEnabled })
                expect (findDeepID (bar, id) != nullptr,
                        "missing Master control for " + juce::String (id));
        }

        beginTest ("Master tab keeps the 3-column knob row");
        {
            ui::BottomBarComponent bar (processor.apvts, processor.getMeterSource(), processor);
            bar.setSize (406, 188);
            bar.getMasterTabs().setCurrentTabIndex (1);

            auto* formant = dynamic_cast<juce::Slider*> (findDeepID (bar, params::IDs::formantShift));
            auto* dryWet = dynamic_cast<juce::Slider*> (findDeepID (bar, params::IDs::dryWet));
            auto* trim = dynamic_cast<juce::Slider*> (findDeepID (bar, params::IDs::outputTrim));
            expect (formant != nullptr && dryWet != nullptr && trim != nullptr,
                    "Master knob row is incomplete");
            if (formant != nullptr && dryWet != nullptr && trim != nullptr)
            {
                expectEquals (formant->getY(), dryWet->getY(), "Master knobs share a row");
                expectEquals (formant->getY(), trim->getY(), "Master knobs share a row");
                expectLessThan (formant->getX(), dryWet->getX(), "Master column order");
                expectLessThan (dryWet->getX(), trim->getX(), "Master column order");
            }
        }

        beginTest ("Growl amount knob is two-way synced");
        {
            ui::BottomBarComponent bar (processor.apvts, processor.getMeterSource(), processor);
            bar.setSize (406, 188);

            auto* param = processor.apvts.getParameter (params::IDs::growlAmount);
            auto* slider = dynamic_cast<juce::Slider*> (findDeepID (bar, params::IDs::growlAmount));
            expect (param != nullptr && slider != nullptr, "growl_amount control missing");
            if (param == nullptr || slider == nullptr)
                return;

            param->setValueNotifyingHost (1.0f);
            expectWithinAbsoluteError (slider->getValue(), 100.0, 0.01, "apvts -> slider");
            slider->setValue (50.0, juce::sendNotificationSync);
            expectWithinAbsoluteError (static_cast<double> (param->getValue()), 0.5, 0.001,
                                       "slider -> apvts");
        }

        beginTest ("formant shift knob is two-way synced");
        {
            ui::BottomBarComponent bar (processor.apvts, processor.getMeterSource(), processor);
            bar.setSize (406, 188);
            bar.getMasterTabs().setCurrentTabIndex (1);

            auto* param = processor.apvts.getParameter (params::IDs::formantShift);
            auto* slider = dynamic_cast<juce::Slider*> (findDeepID (bar, params::IDs::formantShift));
            expect (param != nullptr && slider != nullptr, "formant_shift control missing");
            if (param == nullptr || slider == nullptr)
                return;

            param->setValueNotifyingHost (0.5f);
            expectWithinAbsoluteError (slider->getValue(), 0.0, 0.01,
                                       "apvts -> slider (norm 0.5 = 0 st)");

            slider->setValue (6.0, juce::sendNotificationSync);
            expectWithinAbsoluteError (static_cast<double> (param->getValue()), 0.75, 0.001,
                                       "slider -> apvts (6 st = norm 0.75)");
        }

        beginTest ("dry/wet and trim knobs are two-way synced");
        {
            ui::BottomBarComponent bar (processor.apvts, processor.getMeterSource(), processor);
            bar.setSize (406, 188);
            bar.getMasterTabs().setCurrentTabIndex (1);

            auto* dw = processor.apvts.getParameter (params::IDs::dryWet);
            auto* dwSlider = dynamic_cast<juce::Slider*> (findDeepID (bar, params::IDs::dryWet));
            expect (dw != nullptr && dwSlider != nullptr, "dry_wet control missing");
            if (dw != nullptr && dwSlider != nullptr)
            {
                dw->setValueNotifyingHost (0.4f);
                expectWithinAbsoluteError (dwSlider->getValue(), 40.0, 0.001,
                                           "dry_wet apvts -> slider");
                dwSlider->setValue (60.0, juce::sendNotificationSync);
                expectWithinAbsoluteError (static_cast<double> (dw->getValue()), 0.6, 0.001,
                                           "dry_wet slider -> apvts");
            }

            auto* tr = processor.apvts.getParameter (params::IDs::outputTrim);
            auto* trSlider = dynamic_cast<juce::Slider*> (findDeepID (bar, params::IDs::outputTrim));
            expect (tr != nullptr && trSlider != nullptr, "output_trim control missing");
            if (tr != nullptr && trSlider != nullptr)
            {
                tr->setValueNotifyingHost (0.75f);
                expectWithinAbsoluteError (trSlider->getValue(), 6.0, 0.001,
                                           "output_trim apvts -> slider");
                trSlider->setValue (-3.0, juce::sendNotificationSync);
                expectWithinAbsoluteError (static_cast<double> (tr->getValue()), 0.375, 0.001,
                                           "output_trim slider -> apvts");
            }
        }

        beginTest ("formant toggle is two-way synced");
        {
            ui::BottomBarComponent bar (processor.apvts, processor.getMeterSource(), processor);
            bar.setSize (406, 188);
            bar.getMasterTabs().setCurrentTabIndex (1);

            auto* param = processor.apvts.getParameter (params::IDs::formantEnabled);
            auto* toggle = dynamic_cast<juce::ToggleButton*> (
                findDeepID (bar, params::IDs::formantEnabled));
            expect (param != nullptr && toggle != nullptr, "formant_enabled control missing");
            if (param == nullptr || toggle == nullptr)
                return;

            param->setValueNotifyingHost (1.0f);
            expect (toggle->getToggleState(), "apvts -> toggle (on)");

            toggle->setToggleState (false, juce::sendNotificationSync);
            expectWithinAbsoluteError (static_cast<double> (param->getValue()), 0.0, 0.001,
                                       "toggle -> apvts (off)");
        }

        beginTest ("L/R meter bars use the fixed Growl knob span; no in-card latency");
        {
            ui::BottomBarComponent bar (processor.apvts, processor.getMeterSource(), processor);
            bar.setSize (406, 188);

            const auto l = bar.getMeterBarBounds (0);
            const auto r = bar.getMeterBarBounds (1);
            expectWithinAbsoluteError (l.getX(), 51.5f, 0.6f, "meter left edge");
            expectWithinAbsoluteError (l.getRight(), 353.5f, 0.6f, "meter right edge");
            expectWithinAbsoluteError (l.getHeight(), 12.0f, 0.01f, "meter height");
            expectWithinAbsoluteError (r.getY() - l.getY(), 18.0f, 0.01f,
                                       "L/R row spacing (12 px bar + 6 px gap)");

            expect (findDeepID (bar, "settingsLatency") == nullptr,
                    "latency must not remain in the card");
            for (int i = 0; i < bar.getNumChildComponents(); ++i)
                if (auto* label = dynamic_cast<juce::Label*> (bar.getChildComponent (i)))
                    expect (label->getText() != "Latency", "card must not paint a Latency label");

            expect (findDeepType<ui::BottomBarComponent> (bar) != nullptr,
                    "bottom bar must remain discoverable");
        }

        beginTest ("latency samples -> ms conversion");
        {
            expectWithinAbsoluteError (ui::BottomBarComponent::latencyMs (2048, 48000.0),
                                       42.6667, 0.001, "2048 @ 48k = 42.67 ms");
            expectWithinAbsoluteError (ui::BottomBarComponent::latencyMs (0, 48000.0),
                                       0.0, 0.001, "0 samples = 0 ms");
            expectWithinAbsoluteError (ui::BottomBarComponent::latencyMs (1024, 96000.0),
                                       10.6667, 0.001, "1024 @ 96k = 10.67 ms");
            expectWithinAbsoluteError (ui::BottomBarComponent::latencyMs (2048, 0.0),
                                       0.0, 0.001, "invalid sample rate -> 0");
        }

        beginTest ("meter readLatest drains to the newest peaks");
        {
            ui::MeterSource source;
            source.pushPeaks (0.1f, 0.2f);
            source.pushPeaks (0.3f, 0.4f);
            source.pushPeaks (0.5f, 0.6f);

            float l = 0.0f, r = 0.0f;
            expect (source.readLatest (l, r), "readLatest should succeed");
            expectWithinAbsoluteError (static_cast<double> (l), 0.5, 0.001,
                                       "newest left peak");
            expectWithinAbsoluteError (static_cast<double> (r), 0.6, 0.001,
                                       "newest right peak");
            expectEquals (source.available(), 0, "FIFO drained after readLatest");
        }
    }
};

BottomBarComponentTests bottomBarComponentTests;

} // namespace
