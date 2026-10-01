#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class FabCutieAudioProcessor;

// Temporary editor for testing the EQ engine: pick a band, then edit its
// controls. The frequency-graph UI replaces the body of this component later.
class FabCutieAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FabCutieAudioProcessorEditor (FabCutieAudioProcessor&);
    ~FabCutieAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct LabelledSlider
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
    };

    void selectBand (int bandIndex);

    juce::AudioProcessorValueTreeState& state;

    juce::ComboBox bandSelector;
    juce::ToggleButton bandEnabled { "On" };
    juce::ComboBox bandType, bandSlope, bandPlacement;
    LabelledSlider bandFrequency, bandGain, bandQ;

    std::unique_ptr<ButtonAttachment> bandEnabledAttachment;
    std::unique_ptr<ComboBoxAttachment> bandTypeAttachment, bandSlopeAttachment, bandPlacementAttachment;
    std::unique_ptr<SliderAttachment> bandFrequencyAttachment, bandGainAttachment, bandQAttachment;

    LabelledSlider outputGain;
    juce::ToggleButton bypassButton { "Bypass" };

    std::unique_ptr<SliderAttachment> outputGainAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FabCutieAudioProcessorEditor)
};
