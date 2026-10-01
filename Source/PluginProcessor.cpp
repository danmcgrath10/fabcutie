#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

FabCutieAudioProcessor::FabCutieAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "FabCutieState", fabcutie::params::createLayout())
{
    outputGainDb = state.getRawParameterValue (fabcutie::params::id::outputGain);
    bypass       = state.getRawParameterValue (fabcutie::params::id::bypass);

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

void FabCutieAudioProcessor::pushSoloSettings() noexcept
{
    // Only an enabled band can be soloed, and bypass switches solo off too.
    const auto band = editorLink.soloBand.load();
    fabcutie::dsp::BandSettings settings;

    if (band >= 0 && band < fabcutie::dsp::maxBands && bypass->load() < 0.5f)
        settings = bandParams[(size_t) band].read();

    solo.setBand (settings);
}

void FabCutieAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (samplesPerBlock),
                                        static_cast<juce::uint32> (getTotalNumOutputChannels()) };

    pushBandSettings();
    eq.prepare (sampleRate);

    pushSoloSettings();
    solo.prepare (sampleRate);

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

    // The sidechain (used by the analyzer for now) may be off, mono or stereo.
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

    // The main bus is processed in place; the sidechain only feeds the analyzer.
    auto main = getBusBuffer (buffer, false, 0);
    const auto numChannels = main.getNumChannels();
    const auto numSamples = main.getNumSamples();

    auto* sidechainBus = getBus (true, 1);
    const auto hasSidechain = sidechainBus != nullptr && sidechainBus->isEnabled()
                           && sidechainBus->getNumberOfChannels() > 0;
    editorLink.sidechainConnected.store (hasSidechain, std::memory_order_relaxed);

    const auto analyze = editorLink.analyzerActive.load (std::memory_order_relaxed);

    if (analyze)
    {
        editorLink.pre.push (main.getArrayOfReadPointers(), numChannels, numSamples);

        if (hasSidechain)
        {
            const auto sidechain = getBusBuffer (buffer, true, 1);
            editorLink.external.push (sidechain.getArrayOfReadPointers(), sidechain.getNumChannels(), numSamples);
        }
    }

    pushBandSettings();
    eq.process (main);

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
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FabCutieAudioProcessor();
}
