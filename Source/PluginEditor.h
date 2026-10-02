#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <set>

#include "InstanceRegistry.h"
#include "ui/AnalyzerBar.h"
#include "ui/AssistPanel.h"
#include "ui/BandPanel.h"
#include "ui/EqGraph.h"
#include "ui/EqModel.h"
#include "ui/InstanceList.h"
#include "ui/LevelMeter.h"
#include "ui/MatchPanel.h"
#include "ui/MidiLearnMenu.h"
#include "ui/OutputBar.h"
#include "ui/SpectrumDisplay.h"
#include "ui/Theme.h"
#include "ui/WorkflowBar.h"

class FabCutieAudioProcessor;

// The main window: a header with the instance list, undo, presets, A/B, EQ
// Sketch, EQ Match and Assist, output gain and bypass; the frequency graph with the
// spectrum analyzer behind it and the output meter beside it; a bar
// underneath with the analyzer toggles, character, phase mode, gain scale,
// auto gain and phase invert; and a band panel that floats next to the
// selected node.
//
// The window normally shows its own instance, but the instance list can
// point it at any other FabCutie in the session: everything below the
// header (and the header's controls) then edits that instance instead.
class FabCutieAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private fabcutie::InstanceRegistry::Listener,
                                           private juce::Timer
{
public:
    explicit FabCutieAudioProcessorEditor (FabCutieAudioProcessor&);
    ~FabCutieAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // Everything bound to the instance being edited.
    struct View;

    void setTarget (FabCutieAudioProcessor&);
    FabCutieAudioProcessor* findInstance (int number) const;
    void refreshInstanceList();
    void updateOverlays();
    void updateBandPanel();
    void updateMatchPanel();
    void showToolPanel (juce::TextButton*); // Match or Assist, or nullptr for neither
    void applyAnalyzerSettings (const fabcutie::ui::AnalyzerSettings&);
    bool isEditingOther() const noexcept;

    void instancesChanged() override;
    void instanceRemoved (FabCutieAudioProcessor&) override;
    void timerCallback() override;
    void updatePhaseBox();
    void choosePhase (int itemId);

    fabcutie::ui::LookAndFeel lookAndFeel;
    FabCutieAudioProcessor& owner;
    juce::SharedResourcePointer<fabcutie::InstanceRegistry> registry;

    fabcutie::ui::AnalyzerBar analyzerBar;

    juce::Slider outputGain { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxLeft };
    juce::TextButton bypassButton { "Bypass" };
    juce::ComboBox characterBox;
    juce::TextButton sketchButton { "Sketch" }, matchButton { "Match" }, assistButton { "Assist" };
    juce::ComboBox phaseBox; // phase mode and, for linear phase, its resolution

    juce::TextButton instancesButton, backButton { "Back" };
    fabcutie::ui::InstanceList instanceList;
    std::set<int> shownInstances; // overlaid on the graph, by number

    // After the controls it attaches to, so it is destroyed first.
    std::unique_ptr<View> view;

    bool compactHeader = false; // narrow windows drop the version and the OUTPUT label

    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FabCutieAudioProcessorEditor)
};
