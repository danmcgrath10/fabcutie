#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/KeyBus.h"

class FabCutieAudioProcessor;

namespace fabcutie
{
    // Every FabCutie loaded in this host process, for the instance list: each
    // editor can overlay other instances' curves and edit them in its own
    // window. Shared through juce::SharedResourcePointer, so it lives as long
    // as any instance or editor does.
    //
    // Instances can only see each other when the host loads them into the
    // same process (the usual case; hosts that sandbox every plug-in in its
    // own process show each instance alone).
    class InstanceRegistry final : private juce::AsyncUpdater
    {
    public:
        // Message thread.
        class Listener
        {
        public:
            virtual ~Listener() = default;

            // An instance was added, removed or renamed (called asynchronously).
            virtual void instancesChanged() = 0;

            // Called synchronously while the instance is being destroyed, so
            // editors can let go of it before its state disappears.
            virtual void instanceRemoved (FabCutieAudioProcessor&) {}
        };

        InstanceRegistry() = default;
        ~InstanceRegistry() override { cancelPendingUpdate(); }

        // Returns the instance's number: 1 for the first, then counting up,
        // never reused while the registry lives.
        int add (FabCutieAudioProcessor& instance)
        {
            {
                const juce::ScopedLock sl (lock);
                instances.addIfNotAlreadyThere (&instance);
            }

            notifyChanged();
            return ++lastNumber;
        }

        void remove (FabCutieAudioProcessor& instance)
        {
            {
                const juce::ScopedLock sl (lock);
                instances.removeFirstMatchingValue (&instance);
            }

            listeners.call ([&] (Listener& l) { l.instanceRemoved (instance); });
            notifyChanged();
        }

        // In the order they were created.
        juce::Array<FabCutieAudioProcessor*> getInstances() const
        {
            const juce::ScopedLock sl (lock);
            return instances;
        }

        bool contains (const FabCutieAudioProcessor* instance) const
        {
            const juce::ScopedLock sl (lock);
            return instances.contains (const_cast<FabCutieAudioProcessor*> (instance));
        }

        // Any thread: something listeners show has changed (e.g. a name).
        void notifyChanged() { triggerAsyncUpdate(); }

        // Every instance's output, for auto-unmasking (see KeyBus.h).
        dsp::KeyBusPool& getKeyBuses() noexcept { return keyBuses; }

        void addListener (Listener* l)    { listeners.add (l); }
        void removeListener (Listener* l) { listeners.remove (l); }

    private:
        void handleAsyncUpdate() override
        {
            listeners.call ([] (Listener& l) { l.instancesChanged(); });
        }

        juce::CriticalSection lock;
        juce::Array<FabCutieAudioProcessor*> instances;
        std::atomic<int> lastNumber { 0 };
        juce::ListenerList<Listener> listeners;
        dsp::KeyBusPool keyBuses;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstanceRegistry)
    };
}
