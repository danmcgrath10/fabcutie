#include "Parameters.h"

namespace fabcutie::params
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto dbAttributes = juce::AudioParameterFloatAttributes()
                                .withLabel ("dB")
                                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::outputGain, version },
            "Output Gain",
            juce::NormalisableRange<float> (range::gainMinDb, range::gainMaxDb, 0.01f),
            0.0f,
            dbAttributes));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id::bypass, version },
            "Bypass",
            false));

        // Per-band EQ parameters (frequency, gain, Q, type, slope, stereo mode,
        // enabled) are added here by the EQ engine work.

        return layout;
    }
}
