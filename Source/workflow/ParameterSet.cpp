#include "ParameterSet.h"
#include "Parameters.h"

namespace fabcutie::workflow
{
    ParameterSet::ParameterSet (juce::AudioProcessor& processor)
    {
        for (auto* p : processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);

            if (ranged == nullptr)
                continue;

            const auto& id = ranged->getParameterID();

            if (id == params::id::bypass || id == params::id::pianoRoll)
                continue;

            parameters.push_back (ranged);
        }
    }

    int ParameterSet::indexOf (const juce::String& parameterId) const noexcept
    {
        for (size_t i = 0; i < parameters.size(); ++i)
            if (parameters[i]->getParameterID() == parameterId)
                return (int) i;

        return -1;
    }

    Snapshot ParameterSet::capture() const
    {
        Snapshot values;
        values.reserve (parameters.size());

        for (auto* p : parameters)
            values.push_back (p->getValue());

        return values;
    }

    Snapshot ParameterSet::defaults() const
    {
        Snapshot values;
        values.reserve (parameters.size());

        for (auto* p : parameters)
            values.push_back (p->getDefaultValue());

        return values;
    }

    void ParameterSet::apply (const Snapshot& values) const
    {
        jassert (values.size() == parameters.size());

        for (size_t i = 0; i < parameters.size() && i < values.size(); ++i)
        {
            auto* p = parameters[i];

            if (std::abs (p->getValue() - values[i]) < 1.0e-7f)
                continue;

            p->beginChangeGesture();
            p->setValueNotifyingHost (values[i]);
            p->endChangeGesture();
        }
    }

    juce::ValueTree ParameterSet::toTree (const Snapshot& values, const juce::Identifier& type) const
    {
        juce::ValueTree tree (type);

        for (size_t i = 0; i < parameters.size() && i < values.size(); ++i)
        {
            auto* p = parameters[i];
            tree.setProperty (p->getParameterID(), p->convertFrom0to1 (values[i]), nullptr);
        }

        return tree;
    }

    Snapshot ParameterSet::fromTree (const juce::ValueTree& tree, Snapshot base) const
    {
        base.resize (parameters.size());

        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto* p = parameters[i];

            if (const auto* value = tree.getPropertyPointer (p->getParameterID()))
                base[i] = p->convertTo0to1 ((float) static_cast<double> (*value));
        }

        return base;
    }
}
