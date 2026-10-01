#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

FabCutieAudioProcessor::FabCutieAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state (*this, nullptr, "FabCutieState", fabcutie::params::createLayout())
{
    outputGainDb = state.getRawParameterValue (fabcutie::params::id::outputGain);
    bypass       = state.getRawParameterValue (fabcutie::params::id::bypass);
    character    = state.getRawParameterValue (fabcutie::params::id::character);
    autoGain     = state.getRawParameterValue (fabcutie::params::id::autoGain);
    gainScale    = state.getRawParameterValue (fabcutie::params::id::gainScale);
    phaseInvert  = state.getRawParameterValue (fabcutie::params::id::phaseInvert);

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
        bandParams[(size_t) b].attach (state, b);
}

void FabCutieAudioProcessor::pushBandSettings() noexcept
{
    // Bypass switches every band off, so the EQ fades out (and back in)
    // without clicks instead of jumping.
    const auto bypassed = bypass->load() >= 0.5f;
    const auto scale = gainScale->load() / 100.0f;
    fabcutie::dsp::AutoGain::Bands bands;

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
    {
        auto settings = bandParams[(size_t) b].read();
        settings.enabled = settings.enabled && ! bypassed;
        fabcutie::params::applyGainScale (settings, scale);
        eq.setBand (b, settings);
        bands[(size_t) b] = settings;
    }

    const auto offset = autoGain->load() >= 0.5f ? autoGainStage.update (bands, currentSampleRate) : 0.0f;
    autoGainDb.store (offset, std::memory_order_relaxed);
}

void FabCutieAudioProcessor::pushOutputSettings() noexcept
{
    outputStage.setGainDecibels (outputGainDb->load() + autoGainDb.load (std::memory_order_relaxed),
                                 bypass->load() >= 0.5f,
                                 phaseInvert->load() >= 0.5f);
}

void FabCutieAudioProcessor::pushSoloSettings() noexcept
{
    // Only an enabled band can be soloed, and bypass switches solo off too.
    const auto band = editorLink.soloBand.load();
    fabcutie::dsp::BandSettings settings;

    if (band >= 0 && band < fabcutie::dsp::maxBands && bypass->load() < 0.5f)
        settings = bandParams[(size_t) band].read();

    solo.setBand (settings);
}

void FabCutieAudioProcessor::pushCharacterMode() noexcept
{
    // Bypass fades the character out along with the bands.
    const auto index = bypass->load() >= 0.5f ? 0 : juce::roundToInt (character->load());
    characterStage.setMode ((fabcutie::dsp::CharacterMode) juce::jlimit (0, fabcutie::dsp::numCharacterModes - 1, index));
}

void FabCutieAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (samplesPerBlock),
                                        static_cast<juce::uint32> (getTotalNumOutputChannels()) };

    currentSampleRate = sampleRate;
    pushBandSettings();
    eq.prepare (sampleRate);

    pushSoloSettings();
    solo.prepare (sampleRate);

    pushCharacterMode();
    characterStage.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    pushOutputSettings();
    outputStage.prepare (spec);
}

bool FabCutieAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (out != layouts.getMainInputChannelSet())
        return false;

    // The sidechain can be off, mono or stereo whatever the main layout.
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet (true, 1);

        if (! sidechain.isDisabled() && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void FabCutieAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    midiLearn.process (midi);

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // The main bus is processed in place; the sidechain feeds the analyzer and
    // dynamic bands set to an external source.
    auto main = getBusBuffer (buffer, false, 0);
    const auto numChannels = main.getNumChannels();
    const auto numSamples = main.getNumSamples();

    auto* sidechainBus = getBus (true, 1);
    const auto hasSidechain = sidechainBus != nullptr && sidechainBus->isEnabled()
                           && sidechainBus->getNumberOfChannels() > 0;
    const auto sidechain = hasSidechain ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    editorLink.sidechainConnected.store (hasSidechain, std::memory_order_relaxed);

    const auto analyze = editorLink.analyzerActive.load (std::memory_order_relaxed);

    if (analyze)
    {
        editorLink.pre.push (main.getArrayOfReadPointers(), numChannels, numSamples);

        if (hasSidechain)
            editorLink.external.push (sidechain.getArrayOfReadPointers(), sidechain.getNumChannels(), numSamples);
    }

    pushBandSettings();
    eq.process (main, hasSidechain ? &sidechain : nullptr);

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
        dynamicGains[(size_t) b].store (eq.getDynamicGainDb (b), std::memory_order_relaxed);

    pushCharacterMode();
    characterStage.process (main);

    pushOutputSettings();
    outputStage.process (main);

    pushSoloSettings();
    solo.process (main, numChannels);

    editorLink.outputMeter.process (main, numChannels);

    if (analyze)
        editorLink.post.push (main.getArrayOfReadPointers(), numChannels, numSamples);
}

juce::AudioProcessorEditor* FabCutieAudioProcessor::createEditor()
{
    return new FabCutieAudioProcessorEditor (*this);
}

void FabCutieAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    using namespace fabcutie::workflow;

    // The A/B slot, MIDI map and preset name ride along as child trees.
    auto copy = state.copyState();

    for (const auto& type : { ABCompare::treeType, MidiLearn::treeType, Presets::treeType })
        copy.removeChild (copy.getChildWithName (type), nullptr);

    copy.appendChild (abCompare.toTree(), nullptr);
    copy.appendChild (midiLearn.toTree(), nullptr);
    copy.appendChild (presets.toTree(), nullptr);

    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void FabCutieAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    using namespace fabcutie::workflow;

    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);
    state.replaceState (tree);

    abCompare.fromTree (tree.getChildWithName (ABCompare::treeType));
    midiLearn.fromTree (tree.getChildWithName (MidiLearn::treeType));
    presets.fromTree (tree.getChildWithName (Presets::treeType));

    // A restored session starts a fresh undo history.
    if (juce::MessageManager::existsAndIsCurrentThread())
        history.reset();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FabCutieAudioProcessor();
}
