#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/BandSolo.h"
#include "dsp/EditorLink.h"
#include "dsp/EqEngine.h"
#include "dsp/OutputStage.h"

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

private:
    juce::AudioProcessorValueTreeState state;

    std::atomic<float>* outputGainDb = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::array<fabcutie::params::BandParameterRefs, fabcutie::dsp::maxBands> bandParams;

    fabcutie::dsp::EqEngine eq;
    fabcutie::dsp::OutputStage outputStage;
    fabcutie::dsp::BandSolo solo;
    fabcutie::dsp::EditorLink editorLink;

    void pushBandSettings() noexcept;
    void pushSoloSettings() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FabCutieAudioProcessor)
};
