#pragma once

#include "ParameterSet.h"

namespace fabcutie::workflow
{
    // MIDI CC control of parameters. Arm a parameter with learn(), move a
    // controller, and from then on that CC (on any channel) sets it.
    //
    // The CC map is shared with the audio thread through atomics; the
    // assignments are saved with the session.
    class MidiLearn
    {
    public:
        explicit MidiLearn (const ParameterSet& p) : parameters (p)
        {
            clearAll();
        }

        // Audio thread.
        void process (const juce::MidiBuffer& midi) noexcept
        {
            for (const auto metadata : midi)
            {
                const auto message = metadata.getMessage();

                if (! message.isController())
                    continue;

                const auto cc = message.getControllerNumber();

                if (cc < 0 || cc >= numControllers)
                    continue;

                if (const auto armed = learning.exchange (-1); armed >= 0)
                {
                    // One controller per parameter: drop its old CC.
                    for (auto& target : targets)
                    {
                        auto expected = armed;
                        target.compare_exchange_strong (expected, -1);
                    }

                    targets[(size_t) cc].store (armed);
                }

                const auto index = targets[(size_t) cc].load();

                if (index >= 0 && index < parameters.size())
                    parameters[index].setValueNotifyingHost ((float) message.getControllerValue() / 127.0f);
            }
        }

        // Message thread.
        void learn (int parameterIndex) noexcept { learning.store (parameterIndex); }
        void cancelLearn() noexcept { learning.store (-1); }
        int getLearning() const noexcept { return learning.load(); }

        // The CC assigned to a parameter, or -1.
        int controllerFor (int parameterIndex) const noexcept
        {
            for (int cc = 0; cc < numControllers; ++cc)
                if (targets[(size_t) cc].load() == parameterIndex)
                    return cc;

            return -1;
        }

        void forget (int parameterIndex) noexcept
        {
            for (auto& target : targets)
            {
                auto expected = parameterIndex;
                target.compare_exchange_strong (expected, -1);
            }
        }

        void clearAll() noexcept
        {
            for (auto& target : targets)
                target.store (-1);

            learning.store (-1);
        }

        bool hasAssignments() const noexcept
        {
            for (const auto& target : targets)
                if (target.load() >= 0)
                    return true;

            return false;
        }

        juce::ValueTree toTree() const
        {
            juce::ValueTree tree (treeType);

            for (int cc = 0; cc < numControllers; ++cc)
                if (const auto index = targets[(size_t) cc].load(); index >= 0 && index < parameters.size())
                    tree.setProperty ("cc" + juce::String (cc), parameters[index].getParameterID(), nullptr);

            return tree;
        }

        void fromTree (const juce::ValueTree& tree)
        {
            clearAll();

            if (! tree.hasType (treeType))
                return;

            for (int cc = 0; cc < numControllers; ++cc)
                if (const auto* id = tree.getPropertyPointer ("cc" + juce::String (cc)))
                    targets[(size_t) cc].store (parameters.indexOf (id->toString()));
        }

        static constexpr int numControllers = 128;
        static inline const juce::Identifier treeType { "MidiMap" };

    private:
        const ParameterSet& parameters;
        std::array<std::atomic<int>, numControllers> targets;
        std::atomic<int> learning { -1 };
    };
}
