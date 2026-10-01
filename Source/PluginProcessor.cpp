#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

FabCutieAudioProcessor::FabCutieAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
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

void FabCutieAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (samplesPerBlock),
                                        static_cast<juce::uint32> (getTotalNumOutputChannels()) };

    pushBandSettings();
    eq.prepare (sampleRate);

    outputStage.setGainDecibels (outputGainDb->load(), bypass->load() >= 0.5f);
    outputStage.prepare (spec);
}

bool FabCutieAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

void FabCutieAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    pushBandSettings();
    eq.process (buffer);

    outputStage.setGainDecibels (outputGainDb->load(), bypass->load() >= 0.5f);
    outputStage.process (buffer);
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
