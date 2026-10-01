#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "EqTypes.h"
#include "FilterDesign.h"

namespace fabcutie::dsp
{
    // Auto gain: the output offset that keeps the overall level roughly
    // where it was, worked out from the EQ curve rather than measured, so it
    // never pumps. It is the inverse of the curve's average power gain over
    // 20 Hz - 20 kHz, weighting each octave equally (as pink noise would).
    //
    // Only bands that change the level on purpose count: bells, shelves and
    // tilts. Cuts, notches, band passes and all passes are shaping tools, and
    // compensating them would make a low cut louder. Dynamic bands count at
    // their static gain. Bands on one channel (left, right, mid, side) count
    // half, since they move the level of only half the signal.
    class AutoGain
    {
    public:
        using Bands = std::array<BandSettings, maxBands>;

        // Returns the offset in dB. Cheap when nothing changed, so it can run
        // on every audio block.
        float update (const Bands& bands, double sampleRate) noexcept
        {
            if (! changed (bands, sampleRate))
                return offsetDb;

            last = bands;
            lastSampleRate = sampleRate;
            offsetDb = compute (bands, sampleRate);
            return offsetDb;
        }

        static float compute (const Bands& bands, double sampleRate) noexcept
        {
            std::array<double, numPoints> db {};
            bool any = false;

            for (const auto& s : bands)
            {
                if (! s.enabled || ! countsTowardsLevel (s.type))
                    continue;

                any = true;
                const auto weight = s.placement == Placement::stereo ? 1.0 : 0.5;
                const auto design = designBand (s, sampleRate);

                for (int i = 0; i < numPoints; ++i)
                {
                    const auto mag = std::abs (designResponse (design, pointHz (i), sampleRate));
                    db[(size_t) i] += weight * 20.0 * std::log10 (std::max (mag, 1.0e-6));
                }
            }

            if (! any)
                return 0.0f;

            double power = 0.0;
            for (auto d : db)
                power += std::pow (10.0, d / 10.0);

            const auto averageDb = 10.0 * std::log10 (power / numPoints);
            return (float) std::clamp (-averageDb, -maxOffsetDb, maxOffsetDb);
        }

        static constexpr bool countsTowardsLevel (FilterType t) noexcept
        {
            return t == FilterType::bell || t == FilterType::lowShelf || t == FilterType::highShelf
                || t == FilterType::tiltShelf || t == FilterType::flatTilt;
        }

        static constexpr double maxOffsetDb = 24.0;

    private:
        static constexpr int numPoints = 64;

        static double pointHz (int i) noexcept
        {
            return 20.0 * std::pow (1000.0, (i + 0.5) / numPoints);
        }

        template <typename T>
        static bool differs (T a, T b) noexcept { return a < b || b < a; }

        bool changed (const Bands& bands, double sampleRate) const noexcept
        {
            if (differs (sampleRate, lastSampleRate))
                return true;

            for (size_t b = 0; b < bands.size(); ++b)
            {
                const auto& x = bands[b];
                const auto& y = last[b];

                if (x.enabled != y.enabled || (x.enabled && (! x.sameStructure (y) || differs (x.frequency, y.frequency)
                                                              || differs (x.gainDb, y.gainDb) || differs (x.q, y.q))))
                    return true;
            }

            return false;
        }

        Bands last {};
        double lastSampleRate = 0.0;
        float offsetDb = 0.0f;
    };
}
