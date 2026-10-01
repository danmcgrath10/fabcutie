#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Character.h"
#include "dsp/EqTypes.h"

// Central place for every automatable parameter, so IDs stay stable across
// versions and sessions saved in Logic keep loading.
namespace fabcutie::params
{
    // Version hint passed to juce::ParameterID: the release a parameter first
    // appeared in. Never reuse or rename an existing ID.
    inline constexpr int version = 1;      // output gain, bypass
    inline constexpr int bandsVersion = 2; // per-band EQ parameters
    inline constexpr int characterVersion = 3; // character mode

    namespace id
    {
        inline constexpr auto outputGain = "outputGain";
        inline constexpr auto bypass     = "bypass";
        inline constexpr auto character  = "character";
    }

    namespace range
    {
        inline constexpr float gainMinDb = -36.0f;
        inline constexpr float gainMaxDb =  36.0f;

        inline constexpr float freqMinHz = 10.0f;
        inline constexpr float freqMaxHz = 30000.0f;
        inline constexpr float bandGainMaxDb = 30.0f;
        inline constexpr float qMin = 0.025f;
        inline constexpr float qMax = 40.0f;
    }

    // Per-band parameter IDs look like "b07_freq" (bands are numbered from 1).
    enum class BandParam { enabled, type, frequency, gain, q, slope, placement };

    juce::String bandParamId (int bandIndex, BandParam param);

    juce::StringArray filterTypeNames();
    juce::StringArray slopeNames();
    juce::StringArray placementNames();
    juce::StringArray characterNames();

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Cached raw parameter pointers for reading band settings on the audio thread.
    struct BandParameterRefs
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* slope = nullptr;
        std::atomic<float>* placement = nullptr;

        void attach (juce::AudioProcessorValueTreeState& state, int bandIndex);
        dsp::BandSettings read() const noexcept;
    };
}
