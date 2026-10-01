#include "Parameters.h"

namespace fabcutie::params
{
    namespace
    {
        // Log-scaled ranges so knobs and automation move evenly per octave.
        juce::NormalisableRange<float> logRange (float min, float max)
        {
            return { min, max,
                     [] (float start, float end, float t) { return start * std::pow (end / start, t); },
                     [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
                     [] (float start, float end, float v) { return juce::jlimit (start, end, v); } };
        }

        juce::String frequencyToString (float hz, int)
        {
            if (hz >= 1000.0f)
                return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";

            return juce::String (hz, hz >= 100.0f ? 0 : 1) + " Hz";
        }

        float stringToFrequency (const juce::String& text)
        {
            const auto value = text.getFloatValue();
            return text.containsIgnoreCase ("k") ? value * 1000.0f : value;
        }

        // Spread the default band frequencies log-evenly from 30 Hz to 16 kHz,
        // so a band switched on lands somewhere sensible.
        float defaultFrequency (int bandIndex)
        {
            const auto t = (float) bandIndex / (float) (dsp::maxBands - 1);
            return 30.0f * std::pow (16000.0f / 30.0f, t);
        }

        const char* suffix (BandParam p)
        {
            switch (p)
            {
                case BandParam::enabled:   return "on";
                case BandParam::type:      return "type";
                case BandParam::frequency: return "freq";
                case BandParam::gain:      return "gain";
                case BandParam::q:         return "q";
                case BandParam::slope:     return "slope";
                case BandParam::placement: return "place";
            }

            return "";
        }
    }

    juce::String bandParamId (int bandIndex, BandParam param)
    {
        return "b" + juce::String (bandIndex + 1).paddedLeft ('0', 2) + "_" + suffix (param);
    }

    juce::StringArray filterTypeNames()
    {
        return { "Bell", "Low Shelf", "Low Cut", "High Shelf", "High Cut", "Notch", "Band Pass", "Tilt Shelf" };
    }

    juce::StringArray slopeNames()
    {
        juce::StringArray names;
        for (auto slope : dsp::cutSlopesDbPerOct)
            names.add (juce::String (slope) + " dB/oct");
        return names;
    }

    juce::StringArray placementNames()
    {
        return { "Stereo", "Left", "Right", "Mid", "Side" };
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto dbAttributes = juce::AudioParameterFloatAttributes()
                                .withLabel ("dB")
                                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id::outputGain, version },
            "Output Gain",
            juce::NormalisableRange<float> (range::gainMinDb, range::gainMaxDb, 0.01f),
            0.0f,
            dbAttributes));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { id::bypass, version },
            "Bypass",
            false));

        const auto frequencyAttributes = juce::AudioParameterFloatAttributes()
                                             .withLabel ("Hz")
                                             .withStringFromValueFunction (frequencyToString)
                                             .withValueFromStringFunction (stringToFrequency);

        const auto qAttributes = juce::AudioParameterFloatAttributes()
                                     .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1); });

        const dsp::BandSettings defaults;

        for (int b = 0; b < dsp::maxBands; ++b)
        {
            const auto name = "Band " + juce::String (b + 1) + " ";

            auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
                "band" + juce::String (b + 1), "Band " + juce::String (b + 1), "|");

            group->addChild (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { bandParamId (b, BandParam::enabled), bandsVersion },
                name + "On", defaults.enabled));

            group->addChild (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { bandParamId (b, BandParam::type), bandsVersion },
                name + "Type", filterTypeNames(), (int) defaults.type));

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (b, BandParam::frequency), bandsVersion },
                name + "Frequency", logRange (range::freqMinHz, range::freqMaxHz),
                defaultFrequency (b), frequencyAttributes));

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (b, BandParam::gain), bandsVersion },
                name + "Gain",
                juce::NormalisableRange<float> (-range::bandGainMaxDb, range::bandGainMaxDb, 0.01f),
                defaults.gainDb, dbAttributes));

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (b, BandParam::q), bandsVersion },
                name + "Q", logRange (range::qMin, range::qMax), defaults.q, qAttributes));

            group->addChild (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { bandParamId (b, BandParam::slope), bandsVersion },
                name + "Slope", slopeNames(), defaults.slopeIndex));

            group->addChild (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { bandParamId (b, BandParam::placement), bandsVersion },
                name + "Placement", placementNames(), (int) defaults.placement));

            layout.add (std::move (group));
        }

        return layout;
    }

    void BandParameterRefs::attach (juce::AudioProcessorValueTreeState& state, int bandIndex)
    {
        auto get = [&] (BandParam p)
        {
            auto* value = state.getRawParameterValue (bandParamId (bandIndex, p));
            jassert (value != nullptr);
            return value;
        };

        enabled   = get (BandParam::enabled);
        type      = get (BandParam::type);
        frequency = get (BandParam::frequency);
        gain      = get (BandParam::gain);
        q         = get (BandParam::q);
        slope     = get (BandParam::slope);
        placement = get (BandParam::placement);
    }

    dsp::BandSettings BandParameterRefs::read() const noexcept
    {
        dsp::BandSettings s;
        s.enabled    = enabled->load() >= 0.5f;
        s.type       = (dsp::FilterType) juce::jlimit (0, dsp::numFilterTypes - 1, juce::roundToInt (type->load()));
        s.frequency  = frequency->load();
        s.gainDb     = gain->load();
        s.q          = q->load();
        s.slopeIndex = juce::roundToInt (slope->load());
        s.placement  = (dsp::Placement) juce::jlimit (0, dsp::numPlacements - 1, juce::roundToInt (placement->load()));
        return s;
    }
}
