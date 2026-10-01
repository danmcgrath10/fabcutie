#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "workflow/ABCompare.h"
#include "workflow/History.h"
#include "workflow/MidiLearn.h"
#include "workflow/Presets.h"

namespace fabcutie::ui
{
    // The header's workflow controls: undo / redo, the preset browser, A/B
    // compare and the settings menu (window size, MIDI learn).
    class WorkflowBar final : public juce::Component,
                              private juce::Timer
    {
    public:
        WorkflowBar (workflow::History&, workflow::ABCompare&, workflow::Presets&, workflow::MidiLearn&);
        ~WorkflowBar() override;

        // Called with a window size picked from the settings menu.
        std::function<void (int width, int height)> onSizeChosen;

        // Window sizes offered in the settings menu; the middle one is the default.
        static constexpr std::array<std::pair<int, int>, 4> sizes { { { 900, 520 }, { 1040, 600 }, { 1250, 720 }, { 1500, 865 } } };

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class IconButton;

        void timerCallback() override;
        void update();
        void showPresetMenu();
        void showSettingsMenu();
        void askPresetName();

        workflow::History& history;
        workflow::ABCompare& ab;
        workflow::Presets& presets;
        workflow::MidiLearn& midi;

        std::unique_ptr<IconButton> undoButton, redoButton, previousButton, nextButton, settingsButton;
        juce::TextButton presetButton, aButton { "A" }, bButton { "B" }, copyButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WorkflowBar)
    };
}
