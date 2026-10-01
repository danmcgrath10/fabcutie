#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/AnalyzerBar.h"
#include "ui/BandPanel.h"
#include "ui/EqGraph.h"
#include "ui/EqModel.h"
#include "ui/LevelMeter.h"
#include "ui/MidiLearnMenu.h"
#include "ui/OutputBar.h"
#include "ui/SpectrumDisplay.h"
#include "ui/Theme.h"
#include "ui/WorkflowBar.h"

class FabCutieAudioProcessor;

// The main window: a header with undo, presets, A/B, character, output gain
// and bypass, the frequency graph with the spectrum analyzer behind it and the
// output meter beside it, the analyzer and output bars underneath, and a band
// panel that floats next to the selected node.
class FabCutieAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FabCutieAudioProcessorEditor (FabCutieAudioProcessor&);
    ~FabCutieAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void updateBandPanel();
    void applyAnalyzerSettings (const fabcutie::ui::AnalyzerSettings&);

    fabcutie::ui::LookAndFeel lookAndFeel;
    juce::AudioProcessorValueTreeState& state;
    fabcutie::dsp::EditorLink& link;
    fabcutie::ui::EqModel model;

    fabcutie::ui::EqGraph graph;
    fabcutie::ui::BandPanel bandPanel;
    fabcutie::ui::SpectrumDisplay spectrum;
    fabcutie::ui::LevelMeter meter;
    fabcutie::ui::AnalyzerBar analyzerBar;
    fabcutie::ui::WorkflowBar workflowBar;
    fabcutie::ui::OutputBar outputBar;
    fabcutie::ui::MidiLearnMenu midiLearnMenu;
    fabcutie::workflow::History& history;

    juce::Slider outputGain { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxLeft };
    juce::TextButton bypassButton { "Bypass" };
    juce::ComboBox characterBox;

    std::unique_ptr<SliderAttachment> outputGainAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ComboBoxAttachment> characterAttachment;

    juce::TooltipWindow tooltips { this, 700 };

    // Narrow windows drop the header's text labels to make room for the presets.
    bool compactHeader = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FabCutieAudioProcessorEditor)
};
