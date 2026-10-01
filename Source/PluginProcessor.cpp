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
    phaseMode    = state.getRawParameterValue (fabcutie::params::id::phaseMode);
    linearResolution = state.getRawParameterValue (fabcutie::params::id::linearResolution);

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
        bandParams[(size_t) b].attach (state, b);

    state.addParameterListener (fabcutie::params::id::phaseMode, this);
    state.addParameterListener (fabcutie::params::id::linearResolution, this);
}

FabCutieAudioProcessor::~FabCutieAudioProcessor()
{
    state.removeParameterListener (fabcutie::params::id::phaseMode, this);
    state.removeParameterListener (fabcutie::params::id::linearResolution, this);
    cancelPendingUpdate();
}

fabcutie::dsp::PhaseMode FabCutieAudioProcessor::currentPhaseMode() const noexcept
{
    return (fabcutie::dsp::PhaseMode) juce::jlimit (0, fabcutie::dsp::numPhaseModes - 1, juce::roundToInt (phaseMode->load()));
}

int FabCutieAudioProcessor::currentLinearResolution() const noexcept
{
    return juce::jlimit (0, fabcutie::dsp::numLinearResolutions - 1, juce::roundToInt (linearResolution->load()));
}

void FabCutieAudioProcessor::parameterChanged (const juce::String&, float)
{
    // May arrive on any thread; the host hears about latency on the message thread.
    triggerAsyncUpdate();
}

int FabCutieAudioProcessor::totalLatency() const noexcept
{
    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const auto spectralLatency = spectralRunning.load() ? fabcutie::dsp::SpectralDynamics::latencySamples : 0;
    return fabcutie::dsp::PhaseStage::latencyFor (currentPhaseMode(), currentLinearResolution(), sampleRate) + spectralLatency;
}

void FabCutieAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (totalLatency());
}

void FabCutieAudioProcessor::pushBandSettings() noexcept
{
    // Bypass switches every band off, so the EQ fades out (and back in)
    // without clicks instead of jumping.
    const auto bypassed = isBypassed();

    // In natural and linear phase the FIR runs the static bands and
    // EqEngine only the dynamic ones.
    const auto mode = phaseStage.getMode();
    std::array<fabcutie::dsp::BandSettings, fabcutie::dsp::maxBands> firBands;

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
    {
        auto settings = bandParams[(size_t) b].read();

        // The spectral stage keeps running (and its latency stays put)
        // while bypassed; it just stops changing anything.
        spectral.setBand (b, settings);

        settings.enabled = settings.enabled && ! bypassed;

        const auto iir = fabcutie::dsp::runsAsIir (settings, mode);
        firBands[(size_t) b] = settings;
        firBands[(size_t) b].enabled = settings.enabled && ! iir;

        settings.enabled = settings.enabled && iir;
        eq.setBand (b, settings);
    }

    spectral.setBypassed (bypassed);
    phaseStage.setBands (firBands);
}

void FabCutieAudioProcessor::pushPhaseMode() noexcept
{
    phaseStage.setNonRealtime (isNonRealtime());
    phaseStage.setTarget (currentPhaseMode(), currentLinearResolution());
}

void FabCutieAudioProcessor::pushSoloSettings() noexcept
{
    // Only an enabled band can be soloed, and bypass switches solo off too.
    const auto band = editorLink.soloBand.load();
    fabcutie::dsp::BandSettings settings;

    if (band >= 0 && band < fabcutie::dsp::maxBands && ! isBypassed())
        settings = bandParams[(size_t) band].read();

    solo.setBand (settings);
}

void FabCutieAudioProcessor::pushCharacterMode() noexcept
{
    // Bypass fades the character out along with the bands.
    const auto index = isBypassed() ? 0 : juce::roundToInt (character->load());
    characterStage.setMode ((fabcutie::dsp::CharacterMode) juce::jlimit (0, fabcutie::dsp::numCharacterModes - 1, index));
}

void FabCutieAudioProcessor::updateSpectralStage() noexcept
{
    // The spectral stage delays the signal, so it only runs (and the plugin
    // only reports its latency) while a band uses it. Starting it clears out
    // whatever it held from the last time.
    const auto wanted = spectral.isActive();

    if (wanted == spectralRunning)
        return;

    spectralRunning = wanted;

    if (wanted)
        spectral.reset();

    triggerAsyncUpdate();
}

void FabCutieAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (samplesPerBlock),
                                        static_cast<juce::uint32> (getTotalNumOutputChannels()) };

    // The phase stage starts straight in the session's mode, with its
    // first kernel designed, so playback begins with the right latency.
    {
        std::array<fabcutie::dsp::BandSettings, fabcutie::dsp::maxBands> firBands;
        const auto mode = currentPhaseMode();

        for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
        {
            auto settings = bandParams[(size_t) b].read();
            settings.enabled = settings.enabled && ! isBypassed() && ! fabcutie::dsp::runsAsIir (settings, mode);
            firBands[(size_t) b] = settings;
        }

        phaseStage.setNonRealtime (isNonRealtime());
        phaseStage.prepare (sampleRate, getTotalNumOutputChannels(), mode, currentLinearResolution(), firBands);
        setLatencySamples (phaseStage.getLatency());
    }

    pushBandSettings();
    eq.prepare (sampleRate);
    spectral.prepare (sampleRate);
    spectralRunning = ! spectral.isActive();
    updateSpectralStage();
    setLatencySamples (totalLatency());

    pushSoloSettings();
    solo.prepare (sampleRate);

    pushCharacterMode();
    characterStage.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    outputStage.setGainDecibels (outputGainDb->load(), isBypassed());
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

    // The main bus is processed in place; the sidechain feeds the analyzer and
    // dynamic bands set to an external source.
    auto main = getBusBuffer (buffer, false, 0);
    const auto numChannels = main.getNumChannels();
    const auto numSamples = main.getNumSamples();

    auto* sidechainBus = getBus (true, 1);
    const auto hasSidechain = sidechainBus != nullptr && sidechainBus->isEnabled()
                           && sidechainBus->getNumberOfChannels() > 0;
    auto sidechain = hasSidechain ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    editorLink.sidechainConnected.store (hasSidechain, std::memory_order_relaxed);

    const auto analyze = editorLink.analyzerActive.load (std::memory_order_relaxed);

    if (analyze)
    {
        editorLink.pre.push (main.getArrayOfReadPointers(), numChannels, numSamples);

        if (hasSidechain)
            editorLink.external.push (sidechain.getArrayOfReadPointers(), sidechain.getNumChannels(), numSamples);
    }

    if (editorLink.matchLearning.load (std::memory_order_relaxed))
    {
        editorLink.matchSource.push (main.getArrayOfReadPointers(), numChannels, numSamples);

        if (hasSidechain)
            editorLink.matchReference.push (sidechain.getArrayOfReadPointers(), sidechain.getNumChannels(), numSamples);
    }

    // Natural / linear phase FIR first (it delays the signal, so the
    // sidechain is delayed to match), then the IIR bands.
    pushPhaseMode();
    pushBandSettings();
    phaseStage.process (main);

    if (hasSidechain)
        phaseStage.processSidechain (sidechain);

    eq.process (main, hasSidechain ? &sidechain : nullptr);

    updateSpectralStage();
    if (spectralRunning)
        spectral.process (main, hasSidechain ? &sidechain : nullptr);

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
        dynamicGains[(size_t) b].store (eq.getDynamicGainDb (b) + spectral.getGainDb (b), std::memory_order_relaxed);

    pushCharacterMode();
    characterStage.process (main);

    outputStage.setGainDecibels (outputGainDb->load(), isBypassed());
    outputStage.process (main);

    pushSoloSettings();
    solo.process (main, numChannels);

    editorLink.outputMeter.process (main, numChannels);

    if (analyze)
        editorLink.post.push (main.getArrayOfReadPointers(), numChannels, numSamples);
}

bool FabCutieAudioProcessor::isBypassed() const noexcept
{
    return bypass->load() >= 0.5f || hostBypassed;
}

void FabCutieAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // The host's bypass works like the Bypass button, so the signal keeps
    // the reported latency (the FIR passes it through as a pure delay).
    hostBypassed = true;
    processBlock (buffer, midi);
    hostBypassed = false;
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

            // Let the host know the session's latency before playback starts.
            handleAsyncUpdate();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FabCutieAudioProcessor();
}
