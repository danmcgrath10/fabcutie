#include "History.h"

namespace fabcutie::workflow
{
    History::History (const ParameterSet& p) : parameters (p)
    {
        for (int i = 0; i < parameters.size(); ++i)
            parameters[i].addListener (this);

        reset();
        startTimer (250);
    }

    History::~History()
    {
        for (int i = 0; i < parameters.size(); ++i)
            parameters[i].removeListener (this);
    }

    void History::parameterGestureChanged (int, bool gestureIsStarting)
    {
        if (applying)
            return;

        if (gestureIsStarting)
        {
            openGestures.fetch_add (1);
        }
        else
        {
            // Never below zero, even if a gesture began before we listened.
            for (auto open = openGestures.load(); open > 0;)
                if (openGestures.compare_exchange_weak (open, open - 1))
                    break;

            pending.store (true);
        }
    }

    void History::timerCallback()
    {
        if (openGestures.load() == 0)
            flush();
    }

    void History::flush()
    {
        if (! pending.exchange (false))
            return;

        auto now = parameters.capture();

        if (now == steps[current])
            return;

        steps.resize (current + 1); // a new edit drops the redo steps
        steps.push_back (std::move (now));

        if (steps.size() > maxSteps)
            steps.erase (steps.begin());

        current = steps.size() - 1;
    }

    bool History::canUndo() const noexcept
    {
        return current > 0 || pending.load();
    }

    bool History::canRedo() const noexcept
    {
        return current + 1 < steps.size() && ! pending.load();
    }

    void History::undo()
    {
        flush();

        if (current == 0)
            return;

        // Values may have moved since the last step without a gesture
        // (automation); undo still returns to the step before.
        applyStep (current - 1);
    }

    void History::redo()
    {
        flush();

        if (current + 1 < steps.size())
            applyStep (current + 1);
    }

    void History::applyStep (size_t index)
    {
        current = index;
        const juce::ScopedValueSetter<bool> scope (applying, true);
        parameters.apply (steps[current]);
    }

    void History::applyAsStep (const Snapshot& snapshot)
    {
        flush();

        {
            const juce::ScopedValueSetter<bool> scope (applying, true);
            parameters.apply (snapshot);
        }

        pending.store (true);
        flush();
    }

    void History::reset()
    {
        steps.clear();
        steps.push_back (parameters.capture());
        current = 0;
        pending.store (false);
    }
}
