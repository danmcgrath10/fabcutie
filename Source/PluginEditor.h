#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class FabCutieAudioProcessor;

// Placeholder editor: an output gain knob and a bypass button. The
// frequency-graph UI replaces the body of this component later.
class FabCutieAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FabCutieAudioProcessorEditor (FabCutieAudioProcessor&);
    ~FabCutieAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::Slider outputGain { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label outputGainLabel { {}, "Output" };
    juce::ToggleButton bypassButton { "Bypass" };

    std::unique_ptr<SliderAttachment> outputGainAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FabCutieAudioProcessorEditor)
};
