#include "EqModel.h"
#include "dsp/CurveFit.h"

namespace fabcutie::ui
{
    EqModel::EqModel (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        for (int b = 0; b < dsp::maxBands; ++b)
        {
            reads[(size_t) b].attach (state, b);

            for (int p = 0; p < numBandParams; ++p)
            {
                auto* param = state.getParameter (params::bandParamId (b, (BandParam) p));
                jassert (param != nullptr);
                writes[(size_t) b][(size_t) p] = param;
            }
        }

        pianoRoll = state.getRawParameterValue (params::id::pianoRoll);
        jassert (pianoRoll != nullptr);
    }

    void EqModel::setPianoRoll (bool on)
    {
        if (auto* param = state.getParameter (params::id::pianoRoll))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (on ? 1.0f : 0.0f);
            param->endChangeGesture();
        }
    }

    std::array<dsp::BandSettings, dsp::maxBands> EqModel::getAllBands() const noexcept
    {
        std::array<dsp::BandSettings, dsp::maxBands> bands;
        for (int b = 0; b < dsp::maxBands; ++b)
            bands[(size_t) b] = getBand (b);
        return bands;
    }

    juce::RangedAudioParameter& EqModel::parameter (int band, BandParam p) const noexcept
    {
        return *writes[(size_t) band][(size_t) p];
    }

    void EqModel::set (int band, BandParam p, float value)
    {
        auto& param = parameter (band, p);
        const auto normalised = param.convertTo0to1 (value);

        if (std::abs (normalised - param.getValue()) < 1.0e-7f)
            return;

        const auto inGesture = gestureDepth[(size_t) band][(size_t) p] > 0;

        if (! inGesture) param.beginChangeGesture();
        param.setValueNotifyingHost (normalised);
        if (! inGesture) param.endChangeGesture();
    }

    void EqModel::beginGesture (int band, BandParam p)
    {
        if (gestureDepth[(size_t) band][(size_t) p]++ == 0)
            parameter (band, p).beginChangeGesture();
    }

    void EqModel::endGesture (int band, BandParam p)
    {
        auto& depth = gestureDepth[(size_t) band][(size_t) p];

        if (depth > 0 && --depth == 0)
            parameter (band, p).endChangeGesture();
    }

    int EqModel::findFreeBand() const noexcept
    {
        for (int b = 0; b < dsp::maxBands; ++b)
            if (! getBand (b).enabled)
                return b;

        return -1;
    }

    void EqModel::addBand (int band, dsp::FilterType type, float frequency, float gainDb)
    {
        const dsp::BandSettings defaults;

        set (band, BandParam::type, (float) type);
        set (band, BandParam::frequency, frequency);
        set (band, BandParam::gain, usesGain (type) ? gainDb : 0.0f);
        set (band, BandParam::q, defaults.q);
        set (band, BandParam::slope, (float) defaults.slopeIndex);
        set (band, BandParam::placement, (float) defaults.placement);
        set (band, BandParam::enabled, 1.0f);
    }

    void EqModel::removeBand (int band)
    {
        set (band, BandParam::enabled, 0.0f);
    }

    int EqModel::countFreeBands() const noexcept
    {
        int count = 0;
        for (int b = 0; b < dsp::maxBands; ++b)
            if (! getBand (b).enabled)
                ++count;

        return count;
    }

    std::vector<double> EqModel::getTotalCurve (const std::vector<double>& hz, double sampleRate) const
    {
        return dsp::curvefit::totalCurve (getAllBands(), hz, sampleRate);
    }

    juce::Array<int> EqModel::addBandsForCurve (const std::vector<double>& hz, const std::vector<double>& targetDb,
                                                int maxBands, double sampleRate)
    {
        juce::Array<int> added;

        const auto existing = getTotalCurve (hz, sampleRate);
        auto needed = targetDb;
        for (size_t i = 0; i < needed.size() && i < existing.size(); ++i)
            needed[i] -= existing[i];

        dsp::curvefit::Options options;
        options.maxBands = std::min (maxBands, countFreeBands());
        options.maxGainDb = params::range::bandGainMaxDb;
        options.minQ = std::max (0.1, (double) params::range::qMin);
        options.maxQ = 8.0; // broad strokes: no needle-thin bands chasing small wiggles

        for (const auto& fitted : dsp::curvefit::fit (hz, needed, sampleRate, options))
        {
            const auto band = findFreeBand();
            if (band < 0)
                break;

            const dsp::BandSettings defaults;

            set (band, BandParam::type, (float) fitted.type);
            set (band, BandParam::frequency, fitted.frequency);
            set (band, BandParam::gain, fitted.gainDb);
            set (band, BandParam::q, fitted.q);
            set (band, BandParam::slope, (float) defaults.slopeIndex);
            set (band, BandParam::placement, (float) defaults.placement);
            set (band, BandParam::dynamic, 0.0f);
            set (band, BandParam::enabled, 1.0f);
            added.add (band);
        }

        return added;
    }
}
