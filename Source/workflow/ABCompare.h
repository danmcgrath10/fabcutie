#pragma once

#include "History.h"

namespace fabcutie::workflow
{
    // Two complete settings to flip between. The live parameters are always
    // the active slot; the other slot is kept here and saved with the session.
    class ABCompare
    {
    public:
        ABCompare (const ParameterSet& p, History& h) : parameters (p), history (h) {}

        int getActive() const noexcept { return active; }

        // Stores the live settings in the active slot and loads the other one.
        void select (int slot)
        {
            if (slot == active || slot < 0 || slot > 1)
                return;

            auto& other = slots[(size_t) slot];
            slots[(size_t) active] = parameters.capture();
            active = slot;

            if (other.empty())
                other = slots[(size_t) (1 - slot)]; // first visit starts as a copy

            history.applyAsStep (other);
        }

        // Makes the other slot a copy of the live settings.
        void copyToOther()
        {
            slots[(size_t) (1 - active)] = parameters.capture();
        }

        juce::ValueTree toTree() const
        {
            juce::ValueTree tree (treeType);
            tree.setProperty ("active", active, nullptr);

            if (const auto& other = slots[(size_t) (1 - active)]; ! other.empty())
                tree.appendChild (parameters.toTree (other, "Other"), nullptr);

            return tree;
        }

        void fromTree (const juce::ValueTree& tree)
        {
            slots = {};
            active = 0;

            if (! tree.hasType (treeType))
                return;

            active = juce::jlimit (0, 1, (int) tree.getProperty ("active", 0));

            if (const auto other = tree.getChildWithName ("Other"); other.isValid())
                slots[(size_t) (1 - active)] = parameters.fromTree (other, parameters.defaults());
        }

        static inline const juce::Identifier treeType { "ABCompare" };

    private:
        const ParameterSet& parameters;
        History& history;
        std::array<Snapshot, 2> slots;
        int active = 0;
    };
}
