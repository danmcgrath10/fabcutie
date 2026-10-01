#include "MidiLearnMenu.h"

namespace fabcutie::ui
{
    MidiLearnMenu::MidiLearnMenu (const workflow::ParameterSet& p, workflow::MidiLearn& l)
        : parameters (p), learn (l) {}

    MidiLearnMenu::~MidiLearnMenu()
    {
        for (auto& [component, id] : watched)
            if (component != nullptr)
                component->removeMouseListener (this);
    }

    void MidiLearnMenu::watch (juce::Component& component, std::function<juce::String()> parameterId)
    {
        component.addMouseListener (this, true);
        watched.emplace_back (&component, std::move (parameterId));
    }

    void MidiLearnMenu::mouseDown (const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu())
            return;

        for (auto& [component, parameterId] : watched)
        {
            if (component == nullptr || (e.eventComponent != component && ! component->isParentOf (e.eventComponent)))
                continue;

            const auto index = parameters.indexOf (parameterId());

            if (index < 0)
                return;

            const auto cc = learn.controllerFor (index);
            const auto armed = learn.getLearning() == index;

            juce::PopupMenu menu;
            menu.addSectionHeader (parameters[index].getName (64));
            menu.addItem (1, armed ? "MIDI Learn (move a controller...)" : "MIDI Learn", true, armed);
            menu.addItem (2, cc >= 0 ? "Forget CC " + juce::String (cc) : "No MIDI controller", cc >= 0);

            auto& midi = learn;
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (component.getComponent()).withMousePosition(),
                                [&midi, index, armed] (int result)
                                {
                                    if (result == 1)
                                    {
                                        if (armed) midi.cancelLearn();
                                        else       midi.learn (index);
                                    }
                                    else if (result == 2)
                                    {
                                        midi.forget (index);
                                    }
                                });
            return;
        }
    }
}
