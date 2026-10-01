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
    inline constexpr int pianoRollVersion = 4; // piano roll display
    inline constexpr int dynamicsVersion = 5; // per-band dynamic EQ parameters
    inline constexpr int workflowVersion = 5; // auto gain, gain scale, phase invert

    namespace id
    {
        inline constexpr auto outputGain = "outputGain";
        inline constexpr auto bypass     = "bypass";
        inline constexpr auto character  = "character";
        inline constexpr auto pianoRoll  = "pianoRoll"; // display option: show notes, snap band frequencies to them
        inline constexpr auto autoGain    = "autoGain";    // offset the output by the EQ's average gain change
        inline constexpr auto gainScale   = "gainScale";   // percent: scales every band's gain and dynamic range
        inline constexpr auto phaseInvert = "phaseInvert"; // flips the output polarity
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

        inline constexpr float thresholdMinDb = -80.0f;
        inline constexpr float thresholdMaxDb = 0.0f;
        inline constexpr float dynamicRangeMaxDb = 30.0f;
        inline constexpr float attackMinMs = 0.1f;
        inline constexpr float attackMaxMs = 500.0f;
        inline constexpr float releaseMinMs = 5.0f;
        inline constexpr float releaseMaxMs = 5000.0f;

        inline constexpr float gainScaleMinPercent = -100.0f;
        inline constexpr float gainScaleMaxPercent = 200.0f;
    }

    // Per-band parameter IDs look like "b07_freq" (bands are numbered from 1).
    // Only append: EqModel indexes its parameter table by these values.
    enum class BandParam
    {
        enabled, type, frequency, gain, q, slope, placement,
        dynamic, threshold, range, attack, release, detectorSource, detectorFilter
    };

    inline constexpr int numBandParams = 14;

    juce::String bandParamId (int bandIndex, BandParam param);

    juce::StringArray filterTypeNames();
    juce::StringArray slopeNames();
    juce::StringArray placementNames();
    juce::StringArray characterNames();
    juce::StringArray detectorSourceNames();
    juce::StringArray detectorFilterNames();

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Gain scale applied to a band: its gain and dynamic range, as a factor
    // (1 = 100 %). The processor and the graph both use this.
    inline void applyGainScale (dsp::BandSettings& s, float scale) noexcept
    {
        s.gainDb *= scale;
        s.dynamics.rangeDb *= scale;
    }

    // Parses a frequency typed by hand: "1000", "1.2k", "2 kHz" or a note
    // name such as "A4" or "C#2".
    float parseFrequency (const juce::String& text);

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
        std::atomic<float>* dynamic = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* dynamicRange = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* detectorSource = nullptr;
        std::atomic<float>* detectorFilter = nullptr;

        void attach (juce::AudioProcessorValueTreeState& state, int bandIndex);
        dsp::BandSettings read() const noexcept;
    };
}
