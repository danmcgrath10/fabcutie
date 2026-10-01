#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

namespace
{
    const auto background = juce::Colour (0xff15171c);
    const auto accent     = juce::Colour (0xffff7aa8);

    void setUpSlider (juce::Component& parent, juce::Slider& slider, juce::Label& label, const juce::String& name)
    {
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
        parent.addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        parent.addAndMakeVisible (label);
    }
}

FabCutieAudioProcessorEditor::FabCutieAudioProcessorEditor (FabCutieAudioProcessor& p)
    : AudioProcessorEditor (&p), state (p.getState())
{
    using namespace fabcutie;

    for (int b = 0; b < dsp::maxBands; ++b)
        bandSelector.addItem ("Band " + juce::String (b + 1), b + 1);

    bandSelector.onChange = [this] { selectBand (bandSelector.getSelectedId() - 1); };
    addAndMakeVisible (bandSelector);
    addAndMakeVisible (bandEnabled);

    // ComboBoxAttachment maps choice index i to item id i + 1.
    bandType.addItemList (params::filterTypeNames(), 1);
    bandSlope.addItemList (params::slopeNames(), 1);
    bandPlacement.addItemList (params::placementNames(), 1);

    for (auto* box : { &bandType, &bandSlope, &bandPlacement })
        addAndMakeVisible (*box);

    setUpSlider (*this, bandFrequency.slider, bandFrequency.label, "Frequency");
    setUpSlider (*this, bandGain.slider, bandGain.label, "Gain");
    setUpSlider (*this, bandQ.slider, bandQ.label, "Q");
    setUpSlider (*this, outputGain.slider, outputGain.label, "Output");

    addAndMakeVisible (bypassButton);

    outputGainAttachment = std::make_unique<SliderAttachment> (state, params::id::outputGain, outputGain.slider);
    bypassAttachment     = std::make_unique<ButtonAttachment> (state, params::id::bypass, bypassButton);

    bandSelector.setSelectedId (1); // triggers selectBand (0)

    setResizable (true, true);
    setResizeLimits (640, 300, 1600, 1000);
    setSize (820, 420);
}

void FabCutieAudioProcessorEditor::selectBand (int b)
{
    using namespace fabcutie::params;

    if (b < 0)
        return;

    // Detach from the previous band before attaching to the new one.
    bandEnabledAttachment.reset();
    bandTypeAttachment.reset();
    bandSlopeAttachment.reset();
    bandPlacementAttachment.reset();
    bandFrequencyAttachment.reset();
    bandGainAttachment.reset();
    bandQAttachment.reset();

    bandEnabledAttachment   = std::make_unique<ButtonAttachment>   (state, bandParamId (b, BandParam::enabled), bandEnabled);
    bandTypeAttachment      = std::make_unique<ComboBoxAttachment> (state, bandParamId (b, BandParam::type), bandType);
    bandSlopeAttachment     = std::make_unique<ComboBoxAttachment> (state, bandParamId (b, BandParam::slope), bandSlope);
    bandPlacementAttachment = std::make_unique<ComboBoxAttachment> (state, bandParamId (b, BandParam::placement), bandPlacement);
    bandFrequencyAttachment = std::make_unique<SliderAttachment>   (state, bandParamId (b, BandParam::frequency), bandFrequency.slider);
    bandGainAttachment      = std::make_unique<SliderAttachment>   (state, bandParamId (b, BandParam::gain), bandGain.slider);
    bandQAttachment         = std::make_unique<SliderAttachment>   (state, bandParamId (b, BandParam::q), bandQ.slider);
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

    // Output section on the right.
    auto output = area.removeFromRight (140);
    output.removeFromTop (40);
    outputGain.label.setBounds (output.removeFromTop (24));
    bypassButton.setBounds (output.removeFromBottom (28).withSizeKeepingCentre (90, 28));
    outputGain.slider.setBounds (output.removeFromTop (juce::jmin (output.getHeight(), 160)));

    area.removeFromRight (16);

    // Band selector and choice boxes along the top.
    auto row = area.removeFromTop (28);
    const auto boxWidth = (row.getWidth() - 4 * 8 - 60) / 4;
    bandSelector.setBounds (row.removeFromLeft (boxWidth));
    row.removeFromLeft (8);
    bandEnabled.setBounds (row.removeFromLeft (60));
    row.removeFromLeft (8);
    bandType.setBounds (row.removeFromLeft (boxWidth));
    row.removeFromLeft (8);
    bandSlope.setBounds (row.removeFromLeft (boxWidth));
    row.removeFromLeft (8);
    bandPlacement.setBounds (row);

    area.removeFromTop (12);

    // Three knobs for the selected band.
    auto knobs = area.withHeight (juce::jmin (area.getHeight(), 184));
    const auto knobWidth = knobs.getWidth() / 3;

    for (auto* knob : { &bandFrequency, &bandGain, &bandQ })
    {
        auto cell = knobs.removeFromLeft (knobWidth);
        knob->label.setBounds (cell.removeFromTop (24));
        knob->slider.setBounds (cell);
    }
}
