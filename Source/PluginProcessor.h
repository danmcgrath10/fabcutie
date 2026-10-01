#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "InstanceRegistry.h"
#include "Parameters.h"
#include "dsp/AutoGain.h"
#include "dsp/BandSolo.h"
#include "dsp/ChannelLayout.h"
#include "dsp/EditorLink.h"
#include "dsp/Character.h"
#include "dsp/EqEngine.h"
#include "dsp/OutputStage.h"
#include "dsp/PhaseModes.h"
#include "dsp/SpectralDynamics.h"
#include "ui/EqModel.h"
#include "workflow/ABCompare.h"
#include "workflow/History.h"
#include "workflow/MidiLearn.h"
#include "workflow/ParameterSet.h"
#include "workflow/Presets.h"

class FabCutieAudioProcessor final : public juce::AudioProcessor,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::AsyncUpdater,
                                     private juce::Timer
{
public:
    FabCutieAudioProcessor();
    ~FabCutieAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return JucePlugin_WantsMidiInput != 0; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void updateTrackProperties (const TrackProperties&) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    fabcutie::dsp::EditorLink& getEditorLink() noexcept { return editorLink; }

    // How far each dynamic band is currently moving its gain, for the editor.
    const fabcutie::ui::EqModel::DynamicGains& getDynamicGains() const noexcept { return dynamicGains; }

    // Undo, A/B, presets and MIDI learn (message thread).
    fabcutie::workflow::ParameterSet& getParameterSet() noexcept { return parameterSet; }
    fabcutie::workflow::History& getHistory() noexcept { return history; }
    fabcutie::workflow::ABCompare& getABCompare() noexcept { return abCompare; }
    fabcutie::workflow::Presets& getPresets() noexcept { return presets; }
    fabcutie::workflow::MidiLearn& getMidiLearn() noexcept { return midiLearn; }

    // The output offset auto gain is applying now, in dB.
    float getAutoGainDb() const noexcept { return autoGainDb.load (std::memory_order_relaxed); }

    // Instance list. The number is fixed for the instance's lifetime; the
    // name is the user's own (saved with the session), else the host's track
    // name, else "FabCutie <number>".
    int getInstanceNumber() const noexcept { return instanceNumber; }
    juce::String getInstanceName() const;
    juce::String getCustomInstanceName() const;
    void setCustomInstanceName (const juce::String&); // message thread; empty to clear

    // The current band values, readable from any thread (for overlays).
    std::array<fabcutie::dsp::BandSettings, fabcutie::dsp::maxBands> readBands() const noexcept;

    // The main bus speaker roles playback was prepared with.
    const fabcutie::dsp::ChannelMap& getChannelMap() const noexcept { return channelMap; }

private:
    juce::AudioProcessorValueTreeState state;

    juce::SharedResourcePointer<fabcutie::InstanceRegistry> registry;
    int instanceNumber = 0;
    juce::String trackName;
    juce::SpinLock trackNameLock;
    fabcutie::dsp::ChannelMap channelMap;

    std::atomic<float>* outputGainDb = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* character = nullptr;
    std::atomic<float>* autoGain = nullptr;
    std::atomic<float>* gainScale = nullptr;
    std::atomic<float>* phaseInvert = nullptr;
    std::atomic<float>* phaseMode = nullptr;
    std::atomic<float>* linearResolution = nullptr;
    std::array<fabcutie::params::BandParameterRefs, fabcutie::dsp::maxBands> bandParams;

    fabcutie::dsp::PhaseStage phaseStage;
    fabcutie::dsp::EqEngine eq;
    fabcutie::dsp::SpectralDynamics spectral;
    std::atomic<bool> spectralRunning { false }; // read on the message thread for latency
    std::atomic<bool> surroundLayout { false }; // more than two main channels
    fabcutie::dsp::CharacterStage characterStage;
    fabcutie::dsp::OutputStage outputStage;
    fabcutie::dsp::BandSolo solo;
    fabcutie::dsp::EditorLink editorLink;

    fabcutie::ui::EqModel::DynamicGains dynamicGains {};

    fabcutie::workflow::ParameterSet parameterSet { *this };
    fabcutie::workflow::History history { parameterSet };
    fabcutie::workflow::ABCompare abCompare { parameterSet, history };
    fabcutie::workflow::Presets presets { parameterSet, history };
    fabcutie::workflow::MidiLearn midiLearn { parameterSet };

    // Auto gain is worked out on the message thread (timerCallback), off
    // the audio thread. prepareToPlay and offline renders work it out in
    // place with their own copy, so a bounce is the same every time.
    fabcutie::dsp::AutoGain autoGainStage, audioAutoGainStage;
    std::atomic<float> autoGainDb { 0.0f };
    std::atomic<double> currentSampleRate { 48000.0 };

    fabcutie::dsp::AutoGain::Bands autoGainBands() const noexcept;
    void updateAutoGain (fabcutie::dsp::AutoGain& stage) noexcept;
    void timerCallback() override;

    void pushOutputSettings() noexcept;

    void pushBandSettings() noexcept;
    void pushSoloSettings() noexcept;
    void pushCharacterMode() noexcept;
    void updateSpectralStage() noexcept;
    void pushPhaseMode() noexcept;

    bool hostBypassed = false; // inside processBlockBypassed
    bool isBypassed() const noexcept;

    fabcutie::dsp::PhaseMode currentPhaseMode() const noexcept;
    int currentLinearResolution() const noexcept;
    int totalLatency() const noexcept; // phase mode plus spectral dynamics

    // Latency follows the phase mode and spectral dynamics; the host is told
    // from the message thread.
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FabCutieAudioProcessor)
};
