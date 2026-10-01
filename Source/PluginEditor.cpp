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
}

struct FabCutieAudioProcessorEditor::View
{
    explicit View (FabCutieAudioProcessor& p)
        : processor (p),
          state (p.getState()),
          link (p.getEditorLink()),
          model (state),
          graph (model, [&p] { return p.getSampleRate(); }),
          bandPanel (model),
          spectrum (graph, link, [&p] { return p.getSampleRate(); }),
          meter (link.outputMeter)
    {
        model.setDynamicGainSource (&p.getDynamicGains());

        graph.setBackgroundLayer (&spectrum);
        graph.setPeakSource (&spectrum);
        graph.setSoloTarget (&link.soloBand);
        bandPanel.setSoloTarget (&link.soloBand);
        graph.setRangeDb ((float) state.state.getProperty (rangeDbId, 12.0f));
    }

    ~View()
    {
        // Solo is a listening aid: it never outlives the window.
        link.soloBand.store (-1);
        graph.setPeakSource (nullptr);
        graph.setBackgroundLayer (nullptr);
    }

    FabCutieAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    fabcutie::dsp::EditorLink& link;

    fabcutie::ui::EqModel model;
    fabcutie::ui::EqGraph graph;
    fabcutie::ui::BandPanel bandPanel;
    fabcutie::ui::SpectrumDisplay spectrum;
    fabcutie::ui::LevelMeter meter;

    std::unique_ptr<SliderAttachment> outputGainAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ComboBoxAttachment> characterAttachment;

    bool surround = false;
};

FabCutieAudioProcessorEditor::FabCutieAudioProcessorEditor (FabCutieAudioProcessor& p)
    : AudioProcessorEditor (&p),
      owner (p),
      analyzerBar ([this] { return view != nullptr && view->link.sidechainConnected.load(); })
{
    using namespace fabcutie;

    addAndMakeVisible (analyzerBar);
    analyzerBar.onChange = [this] (const ui::AnalyzerSettings& s) { applyAnalyzerSettings (s); };

    outputGain.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 18);
    outputGain.setTooltip ("Output gain");
    outputGain.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (outputGain);

    bypassButton.setClickingTogglesState (true);
    addAndMakeVisible (bypassButton);

    characterBox.addItemList (params::characterNames(), 1);
    characterBox.setTooltip ("Character: Clean, or Gentle / Warm analog-style saturation (oversampled)");
    addAndMakeVisible (characterBox);

    instancesButton.setTooltip ("Instance list: every FabCutie in the session. Overlay their curves or edit them here.");
    instancesButton.onClick = [this]
    {
        instanceList.setVisible (! instanceList.isVisible());
        refreshInstanceList();
        resized();
    };
    addAndMakeVisible (instancesButton);

    backButton.setTooltip ("Go back to this window's own instance");
    backButton.onClick = [this] { setTarget (owner); };
    addChildComponent (backButton);

    instanceList.onEdit = [this] (int number)
    {
        if (auto* instance = findInstance (number))
        {
            shownInstances.erase (number);
            setTarget (*instance);
        }

        instanceList.setVisible (false);
    };
    instanceList.onShowChanged = [this] (int number, bool shown)
    {
        if (shown) shownInstances.insert (number);
        else       shownInstances.erase (number);

        refreshInstanceList();
        updateOverlays();
    };
    instanceList.onRename = [this] (int number, const juce::String& name)
    {
        if (auto* instance = findInstance (number))
            instance->setCustomInstanceName (name);
    };
    instanceList.onClose = [this] { instanceList.setVisible (false); };
    addChildComponent (instanceList);

    // After the header controls exist, so they all pick it up.
    setLookAndFeel (&lookAndFeel);

    setTarget (owner);
    registry->addListener (this);
    startTimerHz (15);

    setResizable (true, true);
    setResizeLimits (640, 380, 2400, 1500);
    setSize ((int) owner.getState().state.getProperty (editorWidthId, 960),
             (int) owner.getState().state.getProperty (editorHeightId, 580));
}

FabCutieAudioProcessorEditor::~FabCutieAudioProcessorEditor()
{
    registry->removeListener (this);
    view.reset();
    setLookAndFeel (nullptr);
}

bool FabCutieAudioProcessorEditor::isEditingOther() const noexcept
{
    return view != nullptr && &view->processor != &owner;
}

void FabCutieAudioProcessorEditor::setTarget (FabCutieAudioProcessor& target)
{
    using namespace fabcutie;

    if (view != nullptr && &view->processor == &target)
        return;

    view.reset();
    view = std::make_unique<View> (target);
    auto& v = *view;

    addAndMakeVisible (v.graph);
    addChildComponent (v.bandPanel);
    addAndMakeVisible (v.meter);

    v.graph.onRangeChanged = [this] (float db) { view->state.state.setProperty (rangeDbId, db, nullptr); };
    v.graph.onSelectionChanged = [this] { updateBandPanel(); };
    v.graph.onBandsChanged = [this] { updateBandPanel(); };

    ui::AnalyzerSettings analyzerSettings;
    analyzerSettings.load (v.state.state);
    analyzerBar.setSettings (analyzerSettings);
    v.spectrum.setSettings (analyzerSettings);

    v.outputGainAttachment = std::make_unique<SliderAttachment> (v.state, params::id::outputGain, outputGain);
    v.bypassAttachment     = std::make_unique<ButtonAttachment> (v.state, params::id::bypass, bypassButton);
    v.characterAttachment  = std::make_unique<ComboBoxAttachment> (v.state, params::id::character, characterBox);

    // New children pick up the window's look and feel.
    sendLookAndFeelChange();

    backButton.setVisible (isEditingOther());
    instanceList.toFront (false);

    timerCallback();
    refreshInstanceList();
    resized();
    repaint();
}

FabCutieAudioProcessor* FabCutieAudioProcessorEditor::findInstance (int number) const
{
    for (auto* instance : registry->getInstances())
        if (instance->getInstanceNumber() == number)
            return instance;

    return nullptr;
}

void FabCutieAudioProcessorEditor::refreshInstanceList()
{
    std::vector<fabcutie::ui::InstanceList::Row> rows;

    for (auto* instance : registry->getInstances())
    {
        fabcutie::ui::InstanceList::Row row;
        row.number = instance->getInstanceNumber();
        row.name = instance->getInstanceName();
        row.isOwn = instance == &owner;
        row.isEditing = view != nullptr && instance == &view->processor;
        row.shown = ! row.isEditing && shownInstances.count (row.number) > 0;
        rows.push_back (row);
    }

    instanceList.setRows (rows);

    if (view != nullptr)
    {
        const auto& target = view->processor;
        instancesButton.setButtonText (target.getInstanceName() + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")));
        instancesButton.setColour (juce::TextButton::textColourOffId, fabcutie::ui::instanceColour (target.getInstanceNumber()));
    }

    repaint();
}

void FabCutieAudioProcessorEditor::updateOverlays()
{
    if (view == nullptr)
        return;

    std::vector<fabcutie::ui::EqGraph::Overlay> overlays;

    for (auto* instance : registry->getInstances())
    {
        const auto number = instance->getInstanceNumber();

        if (instance == &view->processor || shownInstances.count (number) == 0)
            continue;

        overlays.push_back ({ instance->getInstanceName(), fabcutie::ui::instanceColour (number), instance->readBands() });
    }

    view->graph.setOverlays (std::move (overlays));
}

void FabCutieAudioProcessorEditor::instancesChanged()
{
    refreshInstanceList();
    updateOverlays();
}

void FabCutieAudioProcessorEditor::instanceRemoved (FabCutieAudioProcessor& instance)
{
    shownInstances.erase (instance.getInstanceNumber());

    if (view != nullptr && &view->processor == &instance && &instance != &owner)
        setTarget (owner);
}

void FabCutieAudioProcessorEditor::timerCallback()
{
    updateOverlays();

    if (view == nullptr)
        return;

    const auto surround = view->link.mainChannels.load() > 2;

    if (surround != view->surround)
    {
        view->surround = surround;
        view->graph.setSurround (surround);
        view->bandPanel.setSurround (surround);
    }
}

void FabCutieAudioProcessorEditor::applyAnalyzerSettings (const fabcutie::ui::AnalyzerSettings& s)
{
    if (view == nullptr)
        return;

    s.save (view->state.state);
    analyzerBar.setSettings (s);
    view->spectrum.setSettings (s);
}

void FabCutieAudioProcessorEditor::updateBandPanel()
{
    if (view == nullptr)
        return;

    auto& graph = view->graph;
    auto& bandPanel = view->bandPanel;

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

void FabCutieAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // Frame the graph in the other instance's colour while editing it, so
    // it is obvious this window is not showing its own instance.
    if (! isEditingOther())
        return;

    const auto colour = fabcutie::ui::instanceColour (view->processor.getInstanceNumber());
    const auto frame = view->graph.getBounds().toFloat().reduced (1.0f);
    g.setColour (colour.withAlpha (0.8f));
    g.drawRoundedRectangle (frame, 3.0f, 2.0f);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    const auto label = "EDITING " + view->processor.getInstanceName().toUpperCase();
    const auto labelArea = juce::Rectangle<float> (frame.getX() + 10.0f, frame.getY() + 6.0f, 260.0f, 16.0f);
    g.drawText (label, labelArea, juce::Justification::centredLeft);
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

    // Instance list button after the title and version.
    header.removeFromLeft (150);
    header.removeFromRight (90); // "CHARACTER" label
    auto instances = header.removeFromLeft (juce::jmin (180, header.getWidth()));
    instancesButton.setBounds (instances.withSizeKeepingCentre (instances.getWidth(), 24));
    header.removeFromLeft (6);
    backButton.setBounds (header.removeFromLeft (juce::jmin (56, header.getWidth())).withSizeKeepingCentre (56, 24));

    instanceList.setBounds (instancesButton.getX(), headerHeight,
                            juce::jmin (fabcutie::ui::InstanceList::preferredWidth, getWidth() - instancesButton.getX() - 8),
                            instanceList.getPreferredHeight());

    analyzerBar.setBounds (area.removeFromBottom (analyzerBarHeight));

    if (view != nullptr)
    {
        view->meter.setBounds (area.removeFromRight (meterWidth).withTrimmedTop (8));
        view->graph.setBounds (area);
        updateBandPanel();
    }

    owner.getState().state.setProperty (editorWidthId, getWidth(), nullptr);
    owner.getState().state.setProperty (editorHeightId, getHeight(), nullptr);
}
