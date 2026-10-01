#pragma once

#include <juce_dsp/juce_dsp.h>

namespace fabcutie::dsp
{
    // Final gain stage with click-free smoothing for gain and bypass.
    // The EQ band chain will sit in front of this in PluginProcessor.
    class OutputStage
    {
    public:
        void prepare (const juce::dsp::ProcessSpec& spec)
        {
            gain.reset (spec.sampleRate, rampSeconds);
            gain.setCurrentAndTargetValue (targetGain);
        }

        void setGainDecibels (float db, bool bypassed)
        {
            targetGain = bypassed ? 1.0f : juce::Decibels::decibelsToGain (db);
            gain.setTargetValue (targetGain);
        }

        void process (juce::AudioBuffer<float>& buffer)
        {
            const auto numSamples = buffer.getNumSamples();

            if (! gain.isSmoothing())
            {
                if (! juce::approximatelyEqual (gain.getTargetValue(), 1.0f))
                    buffer.applyGain (gain.getTargetValue());
                return;
            }

            const auto start = gain.getCurrentValue();
            gain.skip (numSamples);
            buffer.applyGainRamp (0, numSamples, start, gain.getCurrentValue());
        }

    private:
        static constexpr double rampSeconds = 0.02;
        juce::LinearSmoothedValue<float> gain { 1.0f };
        float targetGain = 1.0f;
    };
}
