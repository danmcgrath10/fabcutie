#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

namespace
{
    constexpr int headerHeight = 44;
    constexpr int analyzerBarHeight = 32;
    constexpr int meterWidth = 52;

    // Editor settings saved with the session, next to the parameters.
    const juce::Identifier editorWidthId  { "editorWidth" };
    const juce::Identifier editorHeightId { "editorHeight" };
    const juce::Identifier rangeDbId      { "displayRangeDb" };

    // Phase menu item IDs: the two minimum/analog-phase modes, then one per
    // linear phase resolution.
    constexpr int zeroLatencyItem = 1;
    constexpr int naturalItem = 2;
    constexpr int firstLinearItem = 10;
}

FabCutieAudioProcessorEditor::FabCutieAudioProcessorEditor (FabCutieAudioProcessor& p)
    : AudioProcessorEditor (&p),
      state (p.getState()),
      link (p.getEditorLink()),
      model (state),
      graph (model, [&p] { return p.getSampleRate(); }),
      bandPanel (model),
      spectrum (graph, link, [&p] { return p.getSampleRate(); }),
      meter (link.outputMeter),
      analyzerBar ([this] { return link.sidechainConnected.load(); })
{
    using namespace fabcutie;

    model.setDynamicGainSource (&p.getDynamicGains());

    addAndMakeVisible (graph);
    addChildComponent (bandPanel);
    addAndMakeVisible (meter);
    addAndMakeVisible (analyzerBar);

    graph.setBackgroundLayer (&spectrum);
    graph.setPeakSource (&spectrum);
    graph.setSoloTarget (&link.soloBand);
    bandPanel.setSoloTarget (&link.soloBand);

    ui::AnalyzerSettings analyzerSettings;
    analyzerSettings.load (state.state);
    analyzerBar.setSettings (analyzerSettings);
    spectrum.setSettings (analyzerSettings);
    analyzerBar.onChange = [this] (const ui::AnalyzerSettings& s) { applyAnalyzerSettings (s); };

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

    phaseBox.addItem ("Zero Latency", zeroLatencyItem);
    phaseBox.addItem ("Natural Phase", naturalItem);
    phaseBox.addSectionHeading ("Linear Phase");

    const auto resolutions = params::linearResolutionNames();

    for (int r = 0; r < resolutions.size(); ++r)
        phaseBox.addItem ("Linear (" + resolutions[r] + ")", firstLinearItem + r);

    phaseBox.onChange = [this] { choosePhase (phaseBox.getSelectedId()); };
    addAndMakeVisible (phaseBox);

    phaseModeAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (params::id::phaseMode), [this] (float) { updatePhaseBox(); });
    resolutionAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (params::id::linearResolution), [this] (float) { updatePhaseBox(); });
    updatePhaseBox();

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
    // Solo is a listening aid: it never outlives the window.
    link.soloBand.store (-1);
    graph.setPeakSource (nullptr);
    graph.setBackgroundLayer (nullptr);
    setLookAndFeel (nullptr);
}

void FabCutieAudioProcessorEditor::applyAnalyzerSettings (const fabcutie::ui::AnalyzerSettings& s)
{
    s.save (state.state);
    analyzerBar.setSettings (s);
    spectrum.setSettings (s);
}

void FabCutieAudioProcessorEditor::updatePhaseBox()
{
    using namespace fabcutie;

    const auto mode = (dsp::PhaseMode) juce::jlimit (0, dsp::numPhaseModes - 1,
                                                     juce::roundToInt (state.getRawParameterValue (params::id::phaseMode)->load()));
    const auto resolution = juce::jlimit (0, dsp::numLinearResolutions - 1,
                                          juce::roundToInt (state.getRawParameterValue (params::id::linearResolution)->load()));

    const auto item = mode == dsp::PhaseMode::zeroLatency ? zeroLatencyItem
                    : mode == dsp::PhaseMode::natural     ? naturalItem
                                                          : firstLinearItem + resolution;
    phaseBox.setSelectedId (item, juce::dontSendNotification);
    graph.setPhaseMode (mode);

    const auto sampleRate = getAudioProcessor()->getSampleRate() > 0.0 ? getAudioProcessor()->getSampleRate() : 48000.0;
    const auto latencyMs = 1000.0 * dsp::PhaseStage::latencyFor (mode, resolution, sampleRate) / sampleRate;

    phaseBox.setTooltip ("Phase: Zero Latency (minimum phase), Natural Phase (matches analog filters up to Nyquist) "
                         "or Linear Phase (no phase shift; higher resolution is more accurate in the lows but adds latency). "
                         "Current latency: " + juce::String (latencyMs, 1) + " ms");
}

void FabCutieAudioProcessorEditor::choosePhase (int itemId)
{
    using namespace fabcutie;

    if (itemId <= 0)
        return;

    const auto mode = itemId == zeroLatencyItem ? dsp::PhaseMode::zeroLatency
                    : itemId == naturalItem     ? dsp::PhaseMode::natural
                                                : dsp::PhaseMode::linear;

    if (mode == dsp::PhaseMode::linear)
        resolutionAttachment->setValueAsCompleteGesture ((float) (itemId - firstLinearItem));

    phaseModeAttachment->setValueAsCompleteGesture ((float) mode);
    updatePhaseBox();
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

    if (! compactHeader)
    {
        g.drawText ("CHARACTER", characterBox.getBounds().translated (-76, 0).withWidth (72), juce::Justification::centredLeft);
        g.drawText ("PHASE", phaseBox.getBounds().translated (-48, 0).withWidth (44), juce::Justification::centredLeft);
    }
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

    // Below this width the labels would run into the title; the boxes'
    // tooltips still say what they are.
    compactHeader = getWidth() < 860;
    header.removeFromRight (compactHeader ? 8 : 84); // "CHARACTER" label
    phaseBox.setBounds (header.removeFromRight (compactHeader ? 116 : 128).withSizeKeepingCentre (compactHeader ? 116 : 128, 24));

    analyzerBar.setBounds (area.removeFromBottom (analyzerBarHeight));
    meter.setBounds (area.removeFromRight (meterWidth).withTrimmedTop (8));
    graph.setBounds (area);
    updateBandPanel();

    state.state.setProperty (editorWidthId, getWidth(), nullptr);
    state.state.setProperty (editorHeightId, getHeight(), nullptr);
}
