#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace fabcutie::dsp
{
    // The analysis behind the Assist panel: where a track's resonances are,
    // and where it collides with its key (the track that should cut through
    // it). Works on long-term average levels (LongTermSpectrum) on the
    // message thread; the panel turns the results into dynamic bands.
    namespace assist
    {
        struct Suggestion
        {
            double frequency = 1000.0; // Hz
            double q = 1.0;
            double amountDb = 0.0;     // how far the spot stands out (resonances) or how strong the overlap is (collisions)
        };

        // The Q of a bell whose bandwidth is the given number of octaves.
        inline double qForOctaves (double octaves)
        {
            const auto ratio = std::pow (2.0, std::max (octaves, 0.01));
            return std::sqrt (ratio) / (ratio - 1.0);
        }

        // Picks the strongest local peaks of score, at least minSpacing
        // octaves apart. Points with a score of zero or less never count.
        inline std::vector<size_t> strongestPeaks (const std::vector<double>& hz, const std::vector<double>& score,
                                                   int maxCount, double minSpacingOctaves)
        {
            std::vector<size_t> candidates;

            for (size_t i = 0; i < score.size(); ++i)
            {
                const auto left = i > 0 ? score[i - 1] : -1.0e9;
                const auto right = i + 1 < score.size() ? score[i + 1] : -1.0e9;

                if (score[i] > 0.0 && score[i] >= left && score[i] > right)
                    candidates.push_back (i);
            }

            std::sort (candidates.begin(), candidates.end(), [&] (size_t a, size_t b) { return score[a] > score[b]; });

            std::vector<size_t> picked;

            for (auto c : candidates)
            {
                if ((int) picked.size() >= maxCount)
                    break;

                const auto clear = std::none_of (picked.begin(), picked.end(), [&] (size_t p)
                {
                    return std::abs (std::log2 (hz[c] / hz[p])) < minSpacingOctaves;
                });

                if (clear)
                    picked.push_back (c);
            }

            std::sort (picked.begin(), picked.end());
            return picked;
        }

        // How many octaves around peak the condition holds for, walking out
        // from it no further than maxOctaves each way.
        template <typename Holds>
        double widthOctaves (const std::vector<double>& hz, size_t peak, double maxOctaves, Holds&& holds)
        {
            auto lo = peak, hi = peak;

            while (lo > 0 && holds (lo - 1) && std::log2 (hz[peak] / hz[lo - 1]) <= maxOctaves)
                --lo;

            while (hi + 1 < hz.size() && holds (hi + 1) && std::log2 (hz[hi + 1] / hz[peak]) <= maxOctaves)
                ++hi;

            // Each point stands for the space half way to its neighbours.
            const auto step = hz.size() > 1 ? std::log2 (hz[1] / hz[0]) : 0.0;
            return std::log2 (hz[hi] / hz[lo]) + step;
        }

        struct ResonanceOptions
        {
            // Below 150 Hz the peaks are mostly the notes being played.
            double minHz = 150.0, maxHz = 14000.0;
            double sensitivityDb = 5.0; // how far a peak must stand out of its surroundings
            int maxCount = 5;
            double minSpacingOctaves = 1.0 / 3.0;
            double minQ = 2.0, maxQ = 16.0;
        };

        // Resonances: narrow peaks that stand out of the spectrum around
        // them. fineDb is the level at each of hz with little smoothing
        // (1/24 octave or so); localDb the same spectrum smoothed over about
        // an octave, so the tonal balance itself is not mistaken for a peak.
        inline std::vector<Suggestion> findResonances (const std::vector<double>& hz, const std::vector<double>& fineDb,
                                                       const std::vector<double>& localDb, const ResonanceOptions& options = {})
        {
            const auto n = std::min ({ hz.size(), fineDb.size(), localDb.size() });
            std::vector<double> excess (n, 0.0);

            for (size_t i = 0; i < n; ++i)
                if (hz[i] >= options.minHz && hz[i] <= options.maxHz && fineDb[i] > -120.0)
                    excess[i] = std::max (0.0, fineDb[i] - localDb[i] - options.sensitivityDb);

            std::vector<double> hzN (hz.begin(), hz.begin() + (std::ptrdiff_t) n);
            std::vector<Suggestion> out;

            for (auto peak : strongestPeaks (hzN, excess, options.maxCount, options.minSpacingOctaves))
            {
                const auto stands = fineDb[peak] - localDb[peak];
                const auto half = localDb[peak] + 0.5 * stands;
                const auto width = widthOctaves (hzN, peak, 0.5, [&] (size_t i) { return fineDb[i] >= half; });

                Suggestion s;
                s.frequency = hz[peak];
                s.q = std::clamp (qForOctaves (width), options.minQ, options.maxQ);
                s.amountDb = stands;
                out.push_back (s);
            }

            return out;
        }

        struct CollisionOptions
        {
            double minHz = 40.0, maxHz = 10000.0;
            double windowDb = 18.0; // both tracks must be within this of their own loudest point
            int maxCount = 3;
            double minSpacingOctaves = 2.0 / 3.0;
            double minQ = 0.7, maxQ = 4.0;
        };

        // Collisions: where the track and its key are both strong, so the
        // key gets masked. Levels are smoothed (a third of an octave works
        // well) and compared with each signal's own loudest point, so a
        // quieter key still finds where it competes.
        inline std::vector<Suggestion> findCollisions (const std::vector<double>& hz, const std::vector<double>& trackDb,
                                                       const std::vector<double>& keyDb, const CollisionOptions& options = {})
        {
            const auto n = std::min ({ hz.size(), trackDb.size(), keyDb.size() });
            auto trackMax = -1.0e9, keyMax = -1.0e9;

            for (size_t i = 0; i < n; ++i)
            {
                if (hz[i] < options.minHz || hz[i] > options.maxHz)
                    continue;

                trackMax = std::max (trackMax, trackDb[i]);
                keyMax = std::max (keyMax, keyDb[i]);
            }

            std::vector<double> score (n, 0.0);

            auto overlaps = [&] (size_t i)
            {
                return hz[i] >= options.minHz && hz[i] <= options.maxHz
                    && trackDb[i] > -120.0 && keyDb[i] > -120.0
                    && trackDb[i] - trackMax > -options.windowDb && keyDb[i] - keyMax > -options.windowDb;
            };

            for (size_t i = 0; i < n; ++i)
                if (overlaps (i))
                    score[i] = 2.0 * options.windowDb + (trackDb[i] - trackMax) + (keyDb[i] - keyMax);

            std::vector<double> hzN (hz.begin(), hz.begin() + (std::ptrdiff_t) n);
            std::vector<Suggestion> out;

            for (auto peak : strongestPeaks (hzN, score, options.maxCount, options.minSpacingOctaves))
            {
                const auto width = widthOctaves (hzN, peak, 1.0, [&] (size_t i)
                {
                    return overlaps (i) && score[i] >= score[peak] - 6.0;
                });

                Suggestion s;
                s.frequency = hz[peak];
                s.q = std::clamp (qForOctaves (width), options.minQ, options.maxQ);
                s.amountDb = score[peak];
                out.push_back (s);
            }

            return out;
        }
    }
}
