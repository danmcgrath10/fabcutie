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
        int countFreeBands() const noexcept;

        // Switches a band on with fresh settings.
        void addBand (int band, dsp::FilterType type, float frequency, float gainDb);
        void removeBand (int band);

        // EQ Sketch and EQ Match: adds bands in the free slots so the whole
        // EQ draws `targetDb` (dB at each of `hz`) as closely as up to
        // maxBands new bands can. The bands already on stay as they are.
        // Returns the bands that were added.
        juce::Array<int> addBandsForCurve (const std::vector<double>& hz, const std::vector<double>& targetDb,
                                           int maxBands, double sampleRate);

        // The combined static response of the enabled bands, in dB.
        std::vector<double> getTotalCurve (const std::vector<double>& hz, double sampleRate) const;

        // Piano roll display: show notes on the graph and snap band
        // frequencies to them while dragging.
        bool isPianoRollOn() const noexcept { return pianoRoll->load() >= 0.5f; }
        void setPianoRoll (bool on);

        static bool usesGain (dsp::FilterType t) noexcept
        {
            return t == dsp::FilterType::bell || t == dsp::FilterType::lowShelf
                || t == dsp::FilterType::highShelf || t == dsp::FilterType::tiltShelf
                || t == dsp::FilterType::flatTilt;
        }

        static bool usesSlope (dsp::FilterType t) noexcept
        {
            return t == dsp::FilterType::lowCut || t == dsp::FilterType::highCut;
        }

        // Flat tilts and brickwall cuts have no Q.
        static bool usesQ (const dsp::BandSettings& s) noexcept
        {
            return s.type != dsp::FilterType::flatTilt && ! (usesSlope (s.type) && dsp::isBrickwall (s.slopeIndex));
        }

        // Placement names as the band panel and menus show them. In surround
        // they select speakers by side rather than mid/side (see EqEngine).
        static juce::StringArray placementNames (bool surround)
        {
            if (surround)
                return { "All", "Left", "Right", "Centre", "Sides" };

            return params::placementNames();
        }

        static bool usesDynamics (dsp::FilterType t) noexcept { return dsp::supportsDynamics (t); }

        // Live gain offset of each dynamic band, written by the processor.
        using DynamicGains = std::array<std::atomic<float>, dsp::maxBands>;
        void setDynamicGainSource (const DynamicGains* source) noexcept { dynamicGains = source; }
        float getDynamicGainDb (int band) const noexcept
        {
            return dynamicGains != nullptr ? (*dynamicGains)[(size_t) band].load (std::memory_order_relaxed) : 0.0f;
        }

    private:
        static constexpr int numBandParams = params::numBandParams;
        const DynamicGains* dynamicGains = nullptr;

        juce::AudioProcessorValueTreeState& state;
        std::array<params::BandParameterRefs, dsp::maxBands> reads;
        std::array<std::array<juce::RangedAudioParameter*, numBandParams>, dsp::maxBands> writes {};
        std::array<std::array<int, numBandParams>, dsp::maxBands> gestureDepth {};
        std::atomic<float>* pianoRoll = nullptr;
    };
}
