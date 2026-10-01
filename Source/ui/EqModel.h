#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"

namespace fabcutie::ui
{
    // The editor's view of the band parameters. Reads come straight from the
    // parameter values; writes go through the host so they are automatable
    // and undoable, wrapped in change gestures.
    class EqModel
    {
    public:
        using BandParam = params::BandParam;

        explicit EqModel (juce::AudioProcessorValueTreeState&);

        juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

        dsp::BandSettings getBand (int band) const noexcept { return reads[(size_t) band].read(); }
        std::array<dsp::BandSettings, dsp::maxBands> getAllBands() const noexcept;

        juce::RangedAudioParameter& parameter (int band, BandParam p) const noexcept;

        // Sets a value in plain units (Hz, dB, choice index...). Inside a
        // gesture it only moves the value; outside one it is a single edit.
        void set (int band, BandParam p, float value);

        void beginGesture (int band, BandParam p);
        void endGesture (int band, BandParam p);

        // First band that is switched off, or -1 when all are in use.
        int findFreeBand() const noexcept;

        // Switches a band on with fresh settings.
        void addBand (int band, dsp::FilterType type, float frequency, float gainDb);
        void removeBand (int band);

        static bool usesGain (dsp::FilterType t) noexcept
        {
            return t == dsp::FilterType::bell || t == dsp::FilterType::lowShelf
                || t == dsp::FilterType::highShelf || t == dsp::FilterType::tiltShelf;
        }

        static bool usesSlope (dsp::FilterType t) noexcept
        {
            return t == dsp::FilterType::lowCut || t == dsp::FilterType::highCut;
        }

    private:
        static constexpr int numBandParams = 7;

        juce::AudioProcessorValueTreeState& state;
        std::array<params::BandParameterRefs, dsp::maxBands> reads;
        std::array<std::array<juce::RangedAudioParameter*, numBandParams>, dsp::maxBands> writes {};
        std::array<std::array<int, numBandParams>, dsp::maxBands> gestureDepth {};
    };
}
