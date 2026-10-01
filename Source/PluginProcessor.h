#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/BandSolo.h"
#include "dsp/EditorLink.h"
#include "dsp/Character.h"
#include "dsp/EqEngine.h"
#include "dsp/OutputStage.h"
#include "dsp/PhaseModes.h"
#include "ui/EqModel.h"

class FabCutieAudioProcessor final : public juce::AudioProcessor,
                                     private juce::AudioProcessorValueTreeState::Listener,
                                     private juce::AsyncUpdater
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
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    fabcutie::dsp::EditorLink& getEditorLink() noexcept { return editorLink; }

    // How far each dynamic band is currently moving its gain, for the editor.
    const fabcutie::ui::EqModel::DynamicGains& getDynamicGains() const noexcept { return dynamicGains; }

private:
    juce::AudioProcessorValueTreeState state;

    std::atomic<float>* outputGainDb = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* character = nullptr;
    std::atomic<float>* phaseMode = nullptr;
    std::atomic<float>* linearResolution = nullptr;
    std::array<fabcutie::params::BandParameterRefs, fabcutie::dsp::maxBands> bandParams;

    fabcutie::dsp::PhaseStage phaseStage;
    fabcutie::dsp::EqEngine eq;
    fabcutie::dsp::CharacterStage characterStage;
    fabcutie::dsp::OutputStage outputStage;
    fabcutie::dsp::BandSolo solo;
    fabcutie::dsp::EditorLink editorLink;

    fabcutie::ui::EqModel::DynamicGains dynamicGains {};

    void pushBandSettings() noexcept;
    void pushSoloSettings() noexcept;
    void pushCharacterMode() noexcept;
    void pushPhaseMode() noexcept;

    bool hostBypassed = false; // inside processBlockBypassed
    bool isBypassed() const noexcept;

    fabcutie::dsp::PhaseMode currentPhaseMode() const noexcept;
    int currentLinearResolution() const noexcept;

    // Latency follows the phase mode; the host is told from the message thread.
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FabCutieAudioProcessor)
};
