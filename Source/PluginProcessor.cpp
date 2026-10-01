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

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
        bandParams[(size_t) b].attach (state, b);

    instanceNumber = registry->add (*this);
}

FabCutieAudioProcessor::~FabCutieAudioProcessor()
{
    // First, so editors showing this instance let go of it while it is whole.
    registry->remove (*this);
}

namespace
{
    // Saved in the state tree next to the parameters.
    const juce::Identifier instanceNameId { "instanceName" };
}

juce::String FabCutieAudioProcessor::getCustomInstanceName() const
{
    return state.state.getProperty (instanceNameId).toString();
}

void FabCutieAudioProcessor::setCustomInstanceName (const juce::String& name)
{
    const auto trimmed = name.trim().substring (0, 64);

    if (trimmed.isEmpty())
        state.state.removeProperty (instanceNameId, nullptr);
    else
        state.state.setProperty (instanceNameId, trimmed, nullptr);

    registry->notifyChanged();
}

juce::String FabCutieAudioProcessor::getInstanceName() const
{
    if (const auto custom = getCustomInstanceName(); custom.isNotEmpty())
        return custom;

    {
        const juce::SpinLock::ScopedLockType sl (trackNameLock);
        if (trackName.isNotEmpty())
            return trackName;
    }

    return "FabCutie " + juce::String (instanceNumber);
}

void FabCutieAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    {
        const juce::SpinLock::ScopedLockType sl (trackNameLock);
        trackName = properties.name.value_or (juce::String()).trim();
    }

    registry->notifyChanged();
}

std::array<fabcutie::dsp::BandSettings, fabcutie::dsp::maxBands> FabCutieAudioProcessor::readBands() const noexcept
{
    std::array<fabcutie::dsp::BandSettings, fabcutie::dsp::maxBands> bands;

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
        bands[(size_t) b] = bandParams[(size_t) b].read();

    return bands;
}

void FabCutieAudioProcessor::pushBandSettings() noexcept
{
    // Bypass switches every band off, so the EQ fades out (and back in)
    // without clicks instead of jumping.
    const auto bypassed = bypass->load() >= 0.5f;

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
    {
        auto settings = bandParams[(size_t) b].read();
        settings.enabled = settings.enabled && ! bypassed;
        eq.setBand (b, settings);
    }
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

    channelMap = fabcutie::dsp::ChannelMap::fromLayout (getChannelLayoutOfBus (false, 0));
    editorLink.mainChannels.store (channelMap.numChannels);
    eq.setChannelMap (channelMap);

    pushBandSettings();
    eq.prepare (sampleRate);

    pushSoloSettings();
    solo.prepare (sampleRate);

    pushCharacterMode();
    characterStage.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    outputStage.setGainDecibels (outputGainDb->load(), bypass->load() >= 0.5f);
    outputStage.prepare (spec);
}

bool FabCutieAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    // Mono, stereo, or a surround layout up to 9.1.6, the same in and out.
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()
        && ! fabcutie::dsp::isSupportedSurroundLayout (out))
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

void FabCutieAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

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

    const auto analyze = editorLink.analyzerUsers.load (std::memory_order_relaxed) > 0;

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

    outputStage.setGainDecibels (outputGainDb->load(), bypass->load() >= 0.5f);
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
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FabCutieAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            registry->notifyChanged(); // the saved name may differ
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FabCutieAudioProcessor();
}
