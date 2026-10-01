#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_core/juce_core.h>

#include "EqTypes.h"

namespace fabcutie::dsp
{
    // The level detector and gain computer behind a dynamic band.
    //
    // The detector signal (the band's input, or the sidechain) is first
    // filtered to the part of the spectrum the band works on: a band pass for
    // bells, a low pass for low shelves, a high pass for high shelves. Its
    // peak level, linked across channels, is held with the release time and
    // turned into a gain offset in dB for the band, which moves towards a
    // larger offset with the attack time and follows the level back down.
    class BandDynamics
    {
    public:
        static constexpr int maxChannels = 2;

        // Width of the soft knee around the threshold, in dB.
        static constexpr float kneeDb = 6.0f;

        // How far the band's gain moves for a given detector level. Each dB
        // over the threshold moves the gain one dB towards the range, so a
        // negative range holds the band's output at the threshold until the
        // range runs out, and a positive range lifts it by the same amount.
        static float gainOffsetDb (float levelDb, float thresholdDb, float rangeDb) noexcept
        {
            const auto over = levelDb - thresholdDb;
            const auto halfKnee = 0.5f * kneeDb;

            float amount = 0.0f;
            if (over >= halfKnee)
                amount = over;
            else if (over > -halfKnee)
                amount = (over + halfKnee) * (over + halfKnee) / (2.0f * kneeDb);

            return rangeDb < 0.0f ? -std::min (amount, -rangeDb)
                                  :  std::min (amount, rangeDb);
        }

        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            configured = false;
            reset();
        }

        void reset() noexcept
        {
            envelope = 0.0f;
            gainDb = 0.0f;
            state = {};
        }

        // Updates the detector filter and timing. Cheap when nothing changed,
        // so it can be called every control block.
        void configure (const DynamicSettings& d, FilterType type, float frequency, float q) noexcept
        {
            const auto mode = d.filter == DetectorFilter::wide ? Mode::wide
                            : type == FilterType::bell         ? Mode::bandPass
                            : type == FilterType::lowShelf     ? Mode::lowPass
                            : type == FilterType::highShelf    ? Mode::highPass
                                                               : Mode::wide;

            if (configured && mode == filterMode
                && juce::exactlyEqual (frequency, lastFrequency) && juce::exactlyEqual (q, lastQ)
                && juce::exactlyEqual (d.attackMs, lastAttackMs) && juce::exactlyEqual (d.releaseMs, lastReleaseMs))
                return;

            configured = true;
            filterMode = mode;
            lastFrequency = frequency;
            lastQ = q;
            lastAttackMs = d.attackMs;
            lastReleaseMs = d.releaseMs;

            const auto pi = 3.14159265358979323846;
            const auto freq = std::clamp ((double) frequency, 5.0, 0.49 * sampleRate);
            const auto g = std::tan (pi * freq / sampleRate);
            k = mode == Mode::bandPass ? 1.0 / std::max (0.1, (double) q) : std::sqrt (2.0);

            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;

            attackSamples = std::max (1.0e-5, (double) d.attackMs * 0.001) * sampleRate;
            releaseCoeff  = (float) std::exp (-1.0 / (std::max (1.0e-5, (double) d.releaseMs * 0.001) * sampleRate));
        }

        // Follows the detector over numSamples samples. With no channels the
        // detector hears silence, so the envelope releases.
        void detect (const float* const* channels, int numChannels, int numSamples) noexcept
        {
            numChannels = std::min (numChannels, maxChannels);

            for (int i = 0; i < numSamples; ++i)
            {
                double peak = 0.0;

                for (int c = 0; c < numChannels; ++c)
                    peak = std::max (peak, std::abs (filter (state[(size_t) c], (double) channels[c][i])));

                const auto level = (float) peak;
                envelope = level >= envelope ? level : level + releaseCoeff * (envelope - level);
            }

            // Keep a released envelope from drifting into denormals.
            if (envelope < 1.0e-9f)
                envelope = 0.0f;
        }

        float getLevelDb() const noexcept
        {
            return envelope > 0.0f ? 20.0f * std::log10 (envelope) : -200.0f;
        }

        // Moves the gain offset after numSamples of detect(): towards a larger
        // offset with the attack time, straight back as the level falls.
        float updateGain (float thresholdDb, float rangeDb, int numSamples) noexcept
        {
            const auto target = gainOffsetDb (getLevelDb(), thresholdDb, rangeDb);

            if (std::abs (target) > std::abs (gainDb))
                gainDb = target + (float) std::exp (-(double) numSamples / attackSamples) * (gainDb - target);
            else
                gainDb = target;

            if (std::abs (gainDb) < 0.001f)
                gainDb = 0.0f;

            return gainDb;
        }

        float getGainDb() const noexcept { return gainDb; }

    private:
        enum class Mode { wide, bandPass, lowPass, highPass };

        struct State { double ic1 = 0.0, ic2 = 0.0; };

        double filter (State& s, double x) const noexcept
        {
            if (filterMode == Mode::wide)
                return x;

            const auto v3 = x - s.ic2;
            const auto v1 = a1 * s.ic1 + a2 * v3;
            const auto v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
            s.ic1 = 2.0 * v1 - s.ic1;
            s.ic2 = 2.0 * v2 - s.ic2;

            switch (filterMode)
            {
                case Mode::bandPass: return k * v1; // unity gain at the centre
                case Mode::lowPass:  return v2;
                case Mode::highPass: return x - k * v1 - v2;
                case Mode::wide:     break;
            }

            return x;
        }

        double sampleRate = 44100.0;

        bool configured = false;
        Mode filterMode = Mode::wide;
        float lastFrequency = 0.0f, lastQ = 0.0f, lastAttackMs = 0.0f, lastReleaseMs = 0.0f;

        double k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
        double attackSamples = 1.0;
        float releaseCoeff = 0.0f;

        float envelope = 0.0f, gainDb = 0.0f;
        std::array<State, maxChannels> state {};
    };
}
