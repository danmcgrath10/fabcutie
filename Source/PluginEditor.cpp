#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

namespace
{
    constexpr int headerHeight = 44;

    // Editor settings saved with the session, next to the parameters.
    const juce::Identifier editorWidthId  { "editorWidth" };
    const juce::Identifier editorHeightId { "editorHeight" };
    const juce::Identifier rangeDbId      { "displayRangeDb" };
}

FabCutieAudioProcessorEditor::FabCutieAudioProcessorEditor (FabCutieAudioProcessor& p)
    : AudioProcessorEditor (&p),
      state (p.getState()),
      model (state),
      graph (model, [&p] { return p.getSampleRate(); }),
      bandPanel (model)
{
    using namespace fabcutie;

    addAndMakeVisible (graph);
    addChildComponent (bandPanel);

    graph.setRangeDb ((float) state.state.getProperty (rangeDbId, 12.0f));
    graph.onRangeChanged = [this] (float db) { state.state.setProperty (rangeDbId, db, nullptr); };
    graph.onSelectionChanged = [this] { updateBandPanel(); };
    graph.onBandsChanged = [this] { updateBandPanel(); };

    outputGain.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 18);
    outputGain.setTooltip ("Output gain");
    outputGain.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (outputGain);

    bypassButton.setClickingTogglesState (true);
    addAndMakeVisible (bypassButton);

    characterBox.addItemList (params::characterNames(), 1);
    characterBox.setTooltip ("Character: Clean, or Gentle / Warm analog-style saturation (oversampled)");
    addAndMakeVisible (characterBox);

    outputGainAttachment = std::make_unique<SliderAttachment> (state, params::id::outputGain, outputGain);
    bypassAttachment     = std::make_unique<ButtonAttachment> (state, params::id::bypass, bypassButton);
    characterAttachment  = std::make_unique<ComboBoxAttachment> (state, params::id::character, characterBox);

    // After the children exist, so they all pick it up.
    setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    setResizeLimits (640, 380, 2400, 1500);
    setSize ((int) state.state.getProperty (editorWidthId, 960),
             (int) state.state.getProperty (editorHeightId, 580));
}

FabCutieAudioProcessorEditor::~FabCutieAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void FabCutieAudioProcessorEditor::updateBandPanel()
{
    const auto band = graph.getPrimaryBand();
    bandPanel.setBand (band);
    bandPanel.setVisible (band >= 0);

    if (band < 0)
        return;

    // Follow the node horizontally. Sit along the bottom of the graph, or
    // along the top when the node is low enough that the panel would hide it.
    const auto area = graph.getBounds().reduced (10, 0);
    const auto node = graph.getNodePosition (band) + graph.getPosition().toFloat();
    const auto width = juce::jmin (fabcutie::ui::BandPanel::preferredWidth, area.getWidth());
    const auto height = fabcutie::ui::BandPanel::preferredHeight;

    const auto bottomY = area.getBottom() - height - 30;
    const auto y = node.y > (float) bottomY - 24.0f ? area.getY() + 34 : bottomY;
    const auto x = juce::jlimit (area.getX(), area.getRight() - width, juce::roundToInt (node.x) - width / 2);

    bandPanel.setBounds (x, y, width, height);
}

void FabCutieAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace fabcutie::ui;

    g.fillAll (colours::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (16, 0);
    g.setColour (colours::accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("FabCutie", header, juce::Justification::centredLeft);

    g.setColour (colours::textDim.withMultipliedAlpha (0.6f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("v" JucePlugin_VersionString, header.withTrimmedLeft (98), juce::Justification::centredLeft);

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("OUTPUT", outputGain.getBounds().translated (-58, 0), juce::Justification::centredLeft);
    g.drawText ("CHARACTER", characterBox.getBounds().translated (-76, 0).withWidth (72), juce::Justification::centredLeft);
}

void FabCutieAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerHeight).reduced (16, 6);

    bypassButton.setBounds (header.removeFromRight (72).withSizeKeepingCentre (72, 24));
    header.removeFromRight (16);
    outputGain.setBounds (header.removeFromRight (100));
    header.removeFromRight (66); // "OUTPUT" label
    characterBox.setBounds (header.removeFromRight (92).withSizeKeepingCentre (92, 24));

    graph.setBounds (area);
    updateBandPanel();

    state.state.setProperty (editorWidthId, getWidth(), nullptr);
    state.state.setProperty (editorHeightId, getHeight(), nullptr);
}
