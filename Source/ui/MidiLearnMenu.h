#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "workflow/MidiLearn.h"

namespace fabcutie::ui
{
    // Right-click a control for "MIDI Learn" (then move a controller) or to
    // forget its controller. Watches any number of controls.
    class MidiLearnMenu final : private juce::MouseListener
    {
    public:
        MidiLearnMenu (const workflow::ParameterSet&, workflow::MidiLearn&);
        ~MidiLearnMenu() override;

        // `parameterId` is asked at click time, so it can follow a control that
        // is re-attached to another parameter (the band panel's knobs).
        void watch (juce::Component&, std::function<juce::String()> parameterId);

    private:
        void mouseDown (const juce::MouseEvent&) override;

        const workflow::ParameterSet& parameters;
        workflow::MidiLearn& learn;
        std::vector<std::pair<juce::Component::SafePointer<juce::Component>, std::function<juce::String()>>> watched;
    };
}
