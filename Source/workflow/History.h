#pragma once

#include "ParameterSet.h"

namespace fabcutie::workflow
{
    // Undo and redo for edits made in the plugin window.
    //
    // A step is recorded shortly after a change gesture ends (a knob or node
    // drag, a menu choice, a typed value), so a whole drag is one step and
    // quick edits made together (adding a band) merge. Host automation sends
    // no gestures and is never recorded. Message thread only, apart from the
    // listener callbacks, which may come from any thread.
    class History final : private juce::AudioProcessorParameter::Listener,
                          private juce::Timer
    {
    public:
        explicit History (const ParameterSet&);
        ~History() override;

        bool canUndo() const noexcept;
        bool canRedo() const noexcept;
        void undo();
        void redo();

        // Applies a snapshot as one undoable step (A/B switch, preset load).
        void applyAsStep (const Snapshot&);

        // Forgets everything and starts again from the current values, e.g.
        // after the host restores a session.
        void reset();

        // Records any finished edit now instead of waiting for the timer.
        void flush();

        static constexpr size_t maxSteps = 200;

    private:
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool gestureIsStarting) override;
        void timerCallback() override;

        void applyStep (size_t index);

        const ParameterSet& parameters;
        std::vector<Snapshot> steps;
        size_t current = 0;

        std::atomic<int> openGestures { 0 };
        std::atomic<bool> pending { false };
        bool applying = false;

        JUCE_DECLARE_NON_COPYABLE (History)
    };
}
