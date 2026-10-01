#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

namespace
{
    const auto background = juce::Colour (0xff15171c);
    const auto accent     = juce::Colour (0xffff7aa8);
}

FabCutieAudioProcessorEditor::FabCutieAudioProcessorEditor (FabCutieAudioProcessor& p)
    : AudioProcessorEditor (&p)
{
    auto& state = p.getState();

    outputGain.setColour (juce::Slider::rotarySliderFillColourId, accent);
    outputGain.setTextValueSuffix (" dB");
    addAndMakeVisible (outputGain);

    outputGainLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (outputGainLabel);

    addAndMakeVisible (bypassButton);

    outputGainAttachment = std::make_unique<SliderAttachment> (state, fabcutie::params::id::outputGain, outputGain);
    bypassAttachment     = std::make_unique<ButtonAttachment> (state, fabcutie::params::id::bypass, bypassButton);

    setResizable (true, true);
    setResizeLimits (480, 300, 1600, 1000);
    setSize (720, 420);
}

void FabCutieAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = getLocalBounds().removeFromTop (48).reduced (16, 0);
    g.setColour (accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawFittedText ("FabCutie", header, juce::Justification::centredLeft, 1);

    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText ("v" JucePlugin_VersionString, header, juce::Justification::centredRight, 1);
}

void FabCutieAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (40);

    auto controls = area.withSizeKeepingCentre (160, 200);
    outputGainLabel.setBounds (controls.removeFromTop (24));
    bypassButton.setBounds (controls.removeFromBottom (28).withSizeKeepingCentre (90, 28));
    outputGain.setBounds (controls);
}
