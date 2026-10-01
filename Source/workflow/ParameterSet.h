#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace fabcutie::workflow
{
    // The values of every parameter that makes up "the sound" (everything
    // but bypass and display options), normalised, in ParameterSet order.
    using Snapshot = std::vector<float>;

    // The parameters that undo, A/B and presets work on, with helpers to
    // capture, apply, save and load their values.
    class ParameterSet
    {
    public:
        explicit ParameterSet (juce::AudioProcessor&);

        int size() const noexcept { return (int) parameters.size(); }
        juce::RangedAudioParameter& operator[] (int index) const noexcept { return *parameters[(size_t) index]; }
        int indexOf (const juce::String& parameterId) const noexcept;

        Snapshot capture() const;
        Snapshot defaults() const;

        // Moves every parameter that differs to the snapshot's value, each as
        // one host-visible edit.
        void apply (const Snapshot&) const;

        // Saved as plain values keyed by parameter ID, so a later version can
        // change a range or add parameters and old files still load. IDs the
        // tree does not mention keep their value from `base`.
        juce::ValueTree toTree (const Snapshot&, const juce::Identifier& type) const;
        Snapshot fromTree (const juce::ValueTree&, Snapshot base) const;

    private:
        std::vector<juce::RangedAudioParameter*> parameters;
    };
}
