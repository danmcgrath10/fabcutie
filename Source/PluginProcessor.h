#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/AutoGain.h"
#include "dsp/BandSolo.h"
#include "dsp/EditorLink.h"
#include "dsp/Character.h"
#include "dsp/EqEngine.h"
#include "dsp/OutputStage.h"
#include "ui/EqModel.h"
#include "workflow/ABCompare.h"
#include "workflow/History.h"
#include "workflow/MidiLearn.h"
#include "workflow/ParameterSet.h"
#include "workflow/Presets.h"

class FabCutieAudioProcessor final : public juce::AudioProcessor
{
public:
    FabCutieAudioProcessor();
    ~FabCutieAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

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

private:
    juce::AudioProcessorValueTreeState state;

    std::atomic<float>* outputGainDb = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* character = nullptr;
    std::atomic<float>* autoGain = nullptr;
    std::atomic<float>* gainScale = nullptr;
    std::atomic<float>* phaseInvert = nullptr;
    std::array<fabcutie::params::BandParameterRefs, fabcutie::dsp::maxBands> bandParams;

    fabcutie::dsp::EqEngine eq;
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

    fabcutie::dsp::AutoGain autoGainStage;
    std::atomic<float> autoGainDb { 0.0f };
    double currentSampleRate = 48000.0;

    void pushOutputSettings() noexcept;

    void pushBandSettings() noexcept;
    void pushSoloSettings() noexcept;
    void pushCharacterMode() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FabCutieAudioProcessor)
};
