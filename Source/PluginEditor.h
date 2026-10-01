#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ui/AnalyzerBar.h"
#include "ui/BandPanel.h"
#include "ui/EqGraph.h"
#include "ui/EqModel.h"
#include "ui/LevelMeter.h"
#include "ui/MatchPanel.h"
#include "ui/SpectrumDisplay.h"
#include "ui/Theme.h"

class FabCutieAudioProcessor;

// The main window: a header with the EQ Sketch and EQ Match switches,
// character, output gain and bypass, the frequency graph
// with the spectrum analyzer behind it and the output meter beside it, the
// analyzer bar underneath, and a band panel that floats next to the selected
// node.
class FabCutieAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FabCutieAudioProcessorEditor (FabCutieAudioProcessor&);
    ~FabCutieAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void updateBandPanel();
    void updateMatchPanel();
    void applyAnalyzerSettings (const fabcutie::ui::AnalyzerSettings&);
    void updatePhaseBox();
    void choosePhase (int itemId);

    fabcutie::ui::LookAndFeel lookAndFeel;
    juce::AudioProcessorValueTreeState& state;
    fabcutie::dsp::EditorLink& link;
    fabcutie::ui::EqModel model;

    fabcutie::ui::EqGraph graph;
    fabcutie::ui::BandPanel bandPanel;
    fabcutie::ui::SpectrumDisplay spectrum;
    fabcutie::ui::LevelMeter meter;
    fabcutie::ui::AnalyzerBar analyzerBar;
    fabcutie::ui::MatchPanel matchPanel;

    juce::Slider outputGain { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxLeft };
    juce::TextButton bypassButton { "Bypass" };
    juce::ComboBox characterBox;
    juce::TextButton sketchButton { "Sketch" }, matchButton { "Match" };
    juce::ComboBox phaseBox; // phase mode and, for linear phase, its resolution

    std::unique_ptr<SliderAttachment> outputGainAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ComboBoxAttachment> characterAttachment;
    std::unique_ptr<juce::ParameterAttachment> phaseModeAttachment, resolutionAttachment;

    bool compactHeader = false; // narrow windows drop the combo box labels

    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FabCutieAudioProcessorEditor)
};
