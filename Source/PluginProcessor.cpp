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

    pushBandSettings();
    eq.prepare (sampleRate);

    pushCharacterMode();
    characterStage.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    outputStage.setGainDecibels (outputGainDb->load(), bypass->load() >= 0.5f);
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

void FabCutieAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    auto main = getBusBuffer (buffer, true, 0);

    // The sidechain feeds dynamic bands set to an external source.
    const auto* sidechainBus = getBus (true, 1);
    const auto hasSidechain = sidechainBus != nullptr && sidechainBus->isEnabled()
                              && sidechainBus->getNumberOfChannels() > 0;
    const auto sidechain = hasSidechain ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();

    pushBandSettings();
    eq.process (main, hasSidechain ? &sidechain : nullptr);

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
        dynamicGains[(size_t) b].store (eq.getDynamicGainDb (b), std::memory_order_relaxed);

    pushCharacterMode();
    characterStage.process (main);

    outputStage.setGainDecibels (outputGainDb->load(), bypass->load() >= 0.5f);
    outputStage.process (main);
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
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FabCutieAudioProcessor();
}
