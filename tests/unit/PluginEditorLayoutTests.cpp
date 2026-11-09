#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "ui/BottomBarComponent.h"
#include "ui/CrossoverDisplay.h"
#include "ui/TopBarComponent.h"

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

// T64 follow-up: mimic the JUCE StandaloneFilterWindow::MainContentComponent
// — a wrapper between the DocumentWindow and the editor that keeps a 30 px
// strip on top (the "audio input is muted" notification) and follows the
// editor's bounds. The editor must not push its own size into this wrapper.
class StandaloneStyleWrapper final : public juce::Component,
                                     private juce::ComponentListener
{
public:
    static constexpr int stripHeight = 30;

    explicit StandaloneStyleWrapper (juce::Component& editorToHost)
        : editor (editorToHost)
    {
        addAndMakeVisible (editor);
        editor.addComponentListener (this);
        fitToEditor();
    }

    ~StandaloneStyleWrapper() override
    {
        editor.removeComponentListener (this);
    }

    juce::Component& getEditor() const noexcept { return editor; }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (stripHeight);
        editor.setBounds (area);
    }

private:
    void componentMovedOrResized (juce::Component& component, bool, bool) override
    {
        if (&component == &editor)
            fitToEditor();
    }

    void fitToEditor()
    {
        setSize (editor.getWidth(), editor.getHeight() + stripHeight);
    }

    juce::Component& editor;
};

// T63/T73: the editor is a fixed 1200x720 design canvas split into a full-width
// header, an 812 px left column (spectrum/crossover, 70 %) and a 348 px right
// column (band tabs + Master). The Band On switch lives in the tab strip.
class PluginEditorLayoutTests final : public juce::UnitTest
{
public:
    PluginEditorLayoutTests() : juce::UnitTest ("PluginEditorLayout") {}

    void runTest() override
    {
        beginTest ("design canvas and default UI size (T63)");
        {
            expectEquals (PluginEditor::designWidth, 1200);
            expectEquals (PluginEditor::designHeight, 720);

            const auto size100 = PluginEditor::sizeForUserScale (1.0f);
            expectEquals (size100.x, 1020);
            expectEquals (size100.y, 612);

            const auto size75 = PluginEditor::sizeForUserScale (0.75f);
            expectEquals (size75.x, 765);
            expectEquals (size75.y, 459);

            const auto size150 = PluginEditor::sizeForUserScale (1.5f);
            expectEquals (size150.x, 1530);
            expectEquals (size150.y, 918);
        }

        beginTest ("header + 812/348 two-column zones have the expected bounds");
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

            pe->setUiScale (1.0f, false);

            auto* top = findDeepType<ui::TopBarComponent> (*editor);
            auto* display = findDeepType<ui::CrossoverDisplay> (*editor);
            auto* tabs = dynamic_cast<juce::TabbedComponent*> (findDeepID (*editor, "bandTabs"));
            auto* master = findDeepType<ui::BottomBarComponent> (*editor);

            expect (top != nullptr, "TopBarComponent not found");
            expect (display != nullptr, "CrossoverDisplay not found");
            expect (tabs != nullptr, "TabbedComponent not found");
            expect (master != nullptr, "BottomBarComponent not found");
            if (top == nullptr || display == nullptr || tabs == nullptr || master == nullptr)
                return;

            const auto check = [this] (juce::Component& c, int x, int y, int w, int h,
                                       const char* label)
            {
                expectEquals (c.getX(), x, juce::String (label) + " x");
                expectEquals (c.getY(), y, juce::String (label) + " y");
                expectEquals (c.getWidth(), w, juce::String (label) + " width");
                expectEquals (c.getHeight(), h, juce::String (label) + " height");
            };

            check (*top, 14, 14, 1172, 50, "header");
            check (*display, 14, 76, 812, 630, "left column");
            check (*tabs, 838, 76, 348, 430, "right band tabs");
            check (*master, 838, 518, 348, 188, "Master panel");

            // Left and right columns do not overlap; the right column's
            // height is 430 + 12 gutter + 188 = 630. The Band card keeps its
            // 400 px content height; only Master was shortened.
            expectLessThan (display->getRight(), tabs->getX(), "left/right gutter");
            expectEquals (tabs->getBottom() + 12, master->getY(), "right column gutter");
            expectEquals (display->getBottom(), master->getBottom(), "column bottoms align");
        }

        beginTest ("each tab owns a text-free Band On switch left of its label (T63)");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);
            processor.apvts.getParameter (params::IDs::bandCount)->setValueNotifyingHost (1.0f);

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            auto* pe = dynamic_cast<PluginEditor*> (editor.get());
            expect (pe != nullptr, "editor should be a PluginEditor");
            if (pe == nullptr)
                return;
            pe->setUiScale (1.0f, false);

            auto* tabs = dynamic_cast<juce::TabbedComponent*> (findDeepID (*editor, "bandTabs"));
            expect (tabs != nullptr && tabs->getNumTabs() == 3, "three band tabs expected");
            if (tabs == nullptr)
                return;

            juce::ToggleButton* toggles[3] = {};

            for (int i = 0; i < 3; ++i)
            {
                auto* tabButton = tabs->getTabbedButtonBar().getTabButton (i);
                expect (tabButton != nullptr, "tab button missing");
                if (tabButton == nullptr)
                    continue;

                expect (tabButton->getButtonText() == "Band " + juce::String (i + 1),
                        "tab label should be the band name only");

                auto* wrapper = tabButton->getExtraComponent();
                expect (wrapper != nullptr, "tab is missing its Band On wrapper");
                if (wrapper == nullptr)
                    continue;

                const auto enableId = params::makeBandParameterID (i + 1, params::IDs::enable);
                auto* toggle = dynamic_cast<juce::ToggleButton*> (
                    wrapper->findChildWithID (enableId));
                expect (toggle != nullptr, "tab is missing its Band On switch");
                if (toggle == nullptr)
                    continue;

                toggles[i] = toggle;
                expect (toggle->getButtonText().isEmpty(),
                        "tab switch must not carry text");
                expect (toggle->getComponentID() == enableId,
                        "tab switch must bind its own band enable parameter");

                const auto toggleBounds = toggle->getBounds();
                const auto wrapperBounds = wrapper->getBounds();
                const auto textArea = tabButton->getTextArea();
                expect (toggleBounds.getX() >= 6,
                        "tab switch needs a left margin inside the tab");
                expect (toggleBounds.getRight() <= wrapperBounds.getWidth() - 6,
                        "tab switch needs a right margin before the label");
                expect (wrapperBounds.getRight() <= textArea.getX(),
                        "tab switch must sit left of the Band N text");
                expect (! toggleBounds.isEmpty(), "tab switch has zero size");

                auto* param = processor.apvts.getParameter (enableId);
                expect (param != nullptr, "band enable parameter missing");
                if (param != nullptr)
                    expect (toggle->getToggleState() == (param->getValue() > 0.5f),
                            "tab switch must mirror its parameter");
            }

            auto* b2Param = processor.apvts.getParameter ("b2_enable");
            auto* b1Param = processor.apvts.getParameter ("b1_enable");
            expect (b2Param != nullptr && b1Param != nullptr, "band enable params missing");
            if (toggles[1] != nullptr && b2Param != nullptr && b1Param != nullptr)
            {
                toggles[1]->setToggleState (false, juce::sendNotificationSync);
                expectLessThan (b2Param->getValue(), 0.5f, "tab switch -> b2_enable");
                expectGreaterThan (b1Param->getValue(), 0.5f,
                                   "Band 1 must be unaffected by the Band 2 switch");
            }
        }

        beginTest ("P3: band tab selection survives a band-count change");
        {
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            auto* bandCount = processor.apvts.getParameter (params::IDs::bandCount);
            expect (bandCount != nullptr, "band_count parameter missing");
            if (bandCount == nullptr)
                return;

            bandCount->setValueNotifyingHost (1.0f); // 3 bands

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            auto* pe = dynamic_cast<PluginEditor*> (editor.get());
            expect (pe != nullptr, "editor should be a PluginEditor");
            if (pe == nullptr)
                return;

            auto* tabs = dynamic_cast<juce::TabbedComponent*> (findDeepID (*editor, "bandTabs"));
            expect (tabs != nullptr && tabs->getNumTabs() == 3, "three band tabs expected");
            if (tabs == nullptr)
                return;

            tabs->setCurrentTabIndex (2);

            bandCount->setValueNotifyingHost (0.5f); // 2 bands
            // Headless: the editor defers the rebuild to the message thread.
            pe->flushPendingAsyncUpdate();
            expectEquals (tabs->getNumTabs(), 2, "tab count did not follow band_count");
            expectEquals (tabs->getCurrentTabIndex(), 1,
                          "shrinking must clamp to the last band, not reset to Band 1");

            bandCount->setValueNotifyingHost (1.0f); // 3 bands again
            pe->flushPendingAsyncUpdate();
            expectEquals (tabs->getNumTabs(), 3, "tab count did not follow band_count");
            expectEquals (tabs->getCurrentTabIndex(), 1,
                          "growing must keep the previously selected band");
        }

        beginTest ("UI-scale re-adjust keeps the full canvas inside a standalone-style wrapper");
        {
            // Regression (user report): in the Standalone the window's content
            // is a wrapper, not the editor. Resizing that wrapper to the raw
            // editor size dropped the 30 px notification strip from the
            // editor's height, clipping the bottom of the UI on every scale
            // change after launch.
            PluginProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            auto* pe = dynamic_cast<PluginEditor*> (editor.get());
            expect (pe != nullptr, "editor should be a PluginEditor");
            if (pe == nullptr)
                return;

            pe->setUiScale (1.0f, false);

            StandaloneStyleWrapper wrapper (*editor);
            juce::DocumentWindow window ("host", juce::Colour (0xff000000),
                                         juce::DocumentWindow::closeButton, false);
            window.setContentNonOwned (&wrapper, true);

            for (const float scale : { 1.0f, 1.25f, 0.8f, 1.5f, 0.75f })
            {
                pe->setUiScale (scale, false);

                const auto expected = PluginEditor::sizeForUserScale (scale);
                expectEquals (editor->getWidth(), expected.x, "editor width after scale change");
                expectEquals (editor->getHeight(), expected.y, "editor height after scale change");
                expectEquals (wrapper.getHeight(),
                              expected.y + StandaloneStyleWrapper::stripHeight,
                              "wrapper must keep the notification strip above the editor");
            }

            window.clearContentComponent();
        }
    }
};

PluginEditorLayoutTests pluginEditorLayoutTests;

} // namespace
