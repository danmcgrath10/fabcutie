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
    phaseMode    = state.getRawParameterValue (fabcutie::params::id::phaseMode);
    linearResolution = state.getRawParameterValue (fabcutie::params::id::linearResolution);

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
        bandParams[(size_t) b].attach (state, b);

    instanceNumber = registry->add (*this);
    state.addParameterListener (fabcutie::params::id::phaseMode, this);
    state.addParameterListener (fabcutie::params::id::linearResolution, this);
    startTimerHz (30);
}

FabCutieAudioProcessor::~FabCutieAudioProcessor()
{
    // First, so editors showing this instance let go of it while it is whole.
    registry->remove (*this);
    stopTimer();

    state.removeParameterListener (fabcutie::params::id::phaseMode, this);
    state.removeParameterListener (fabcutie::params::id::linearResolution, this);
    cancelPendingUpdate();
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

fabcutie::dsp::PhaseMode FabCutieAudioProcessor::currentPhaseMode() const noexcept
{
    // The FIR and the spectral stage are stereo, so surround layouts run
    // every band as IIR, with no latency.
    if (surroundLayout.load())
        return fabcutie::dsp::PhaseMode::zeroLatency;

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
    const auto scale = gainScale->load() / 100.0f;

    // In natural and linear phase the FIR runs the static bands and
    // EqEngine only the dynamic ones.
    const auto mode = phaseStage.getMode();
    std::array<fabcutie::dsp::BandSettings, fabcutie::dsp::maxBands> firBands;

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
    {
        auto settings = bandParams[(size_t) b].read();

        // In surround spectral bands work as ordinary dynamic bands.
        if (surroundLayout.load (std::memory_order_relaxed))
            settings.dynamics.spectral = false;

        fabcutie::params::applyGainScale (settings, scale);

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

    // Live, the message thread keeps auto gain up to date; offline it is
    // worked out here so every block uses the curve it was rendered with.
    if (isNonRealtime())
        updateAutoGain (audioAutoGainStage);
}

fabcutie::dsp::AutoGain::Bands FabCutieAudioProcessor::autoGainBands() const noexcept
{
    // The bands as they sound, gain scale included. Bypass is left out:
    // the output stage ignores the offset while bypassed anyway, and this
    // way it is ready the moment bypass goes off.
    const auto scale = gainScale->load() / 100.0f;
    fabcutie::dsp::AutoGain::Bands bands;

    for (int b = 0; b < fabcutie::dsp::maxBands; ++b)
    {
        auto settings = bandParams[(size_t) b].read();
        fabcutie::params::applyGainScale (settings, scale);
        bands[(size_t) b] = settings;
    }

    return bands;
}

void FabCutieAudioProcessor::updateAutoGain (fabcutie::dsp::AutoGain& stage) noexcept
{
    const auto offset = autoGain->load() >= 0.5f ? stage.update (autoGainBands(), currentSampleRate.load()) : 0.0f;
    autoGainDb.store (offset, std::memory_order_relaxed);
}

void FabCutieAudioProcessor::timerCallback()
{
    // Cheap when nothing changed: AutoGain only recomputes on a change.
    if (! isNonRealtime())
        updateAutoGain (autoGainStage);
}

void FabCutieAudioProcessor::pushOutputSettings() noexcept
{
    outputStage.setGainDecibels (outputGainDb->load() + autoGainDb.load (std::memory_order_relaxed),
                                 isBypassed(),
                                 phaseInvert->load() >= 0.5f);
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

    currentSampleRate = sampleRate;

    channelMap = fabcutie::dsp::ChannelMap::fromLayout (getChannelLayoutOfBus (false, 0));
    editorLink.mainChannels.store (channelMap.numChannels);
    eq.setChannelMap (channelMap);
    surroundLayout.store (channelMap.numChannels > 2);

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
    updateAutoGain (audioAutoGainStage);
    eq.prepare (sampleRate);
    spectral.prepare (sampleRate);
    spectralRunning = ! spectral.isActive();
    updateSpectralStage();
    setLatencySamples (totalLatency());

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
    auto sidechain = hasSidechain ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    editorLink.sidechainConnected.store (hasSidechain, std::memory_order_relaxed);

    const auto analyze = editorLink.analyzerUsers.load (std::memory_order_relaxed) > 0;

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

    pushOutputSettings();
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
    registry->notifyChanged(); // the saved name may differ

    // Let the host know the session's latency before playback starts.
    handleAsyncUpdate();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FabCutieAudioProcessor();
}
