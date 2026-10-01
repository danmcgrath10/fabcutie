#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "FilterDesign.h"

namespace fabcutie::dsp
{
    // Turns a target EQ curve (dB at a list of frequencies) into a handful of
    // bands that draw it. Used by EQ Sketch (a curve drawn with the mouse)
    // and EQ Match (the difference between two spectra).
    //
    // The fit is greedy: it finds the frequency where the curve is furthest
    // from what the bands so far already do, tries a bell there (and a low or
    // high shelf when that part of the curve runs off the end of the
    // spectrum), tunes the candidates' frequency, gain and Q by coordinate
    // descent on the squared error, keeps the best one, and repeats. A final
    // pass retunes every band with the others in place.
    namespace curvefit
    {
        struct Options
        {
            int maxBands = 8;
            double toleranceDb = 0.4;  // stop once the curve is this close everywhere
            double maxGainDb = 24.0;
            double minQ = 0.1, maxQ = 18.0;
            double minHz = 20.0, maxHz = 20000.0;
        };

        // Log-spaced frequencies, `perOctave` points per octave, lowHz..highHz.
        inline std::vector<double> logFrequencies (double lowHz = 20.0, double highHz = 20000.0, int perOctave = 12)
        {
            std::vector<double> hz;
            const auto octaves = std::log2 (highHz / lowHz);
            const auto n = (int) std::ceil (octaves * perOctave);

            for (int i = 0; i <= n; ++i)
                hz.push_back (lowHz * std::pow (2.0, octaves * i / n));

            return hz;
        }

        // A band's response in dB at every frequency.
        inline void bandCurve (const BandSettings& s, const std::vector<double>& hz, double sampleRate, std::vector<double>& out)
        {
            const auto design = designBand (s, sampleRate);
            out.resize (hz.size());

            for (size_t i = 0; i < hz.size(); ++i)
                out[i] = 20.0 * std::log10 (std::max (std::abs (designResponse (design, hz[i], sampleRate)), 1.0e-9));
        }

        // The combined response of every enabled band, in dB.
        template <typename Bands>
        std::vector<double> totalCurve (const Bands& bands, const std::vector<double>& hz, double sampleRate)
        {
            std::vector<double> total (hz.size(), 0.0), one;

            for (const auto& s : bands)
            {
                if (! s.enabled)
                    continue;

                auto stat = s;
                stat.dynamics = {};
                bandCurve (stat, hz, sampleRate, one);

                for (size_t i = 0; i < hz.size(); ++i)
                    total[i] += one[i];
            }

            return total;
        }

        namespace detail
        {
            inline double squaredError (const std::vector<double>& residual, const std::vector<double>& curve) noexcept
            {
                auto sum = 0.0;
                for (size_t i = 0; i < residual.size(); ++i)
                {
                    const auto e = residual[i] - curve[i];
                    sum += e * e;
                }
                return sum;
            }

            struct Candidate
            {
                BandSettings band;
                double error = 0.0;
            };

            // Tunes log2(frequency), gain and log2(Q) to fit the residual.
            inline Candidate tune (BandSettings s, const std::vector<double>& hz, const std::vector<double>& residual,
                                   double sampleRate, const Options& o)
            {
                std::vector<double> curve;

                auto clampBand = [&] (BandSettings& b)
                {
                    b.frequency = (float) std::clamp ((double) b.frequency, o.minHz, std::min (o.maxHz, 0.45 * sampleRate));
                    b.gainDb = (float) std::clamp ((double) b.gainDb, -o.maxGainDb, o.maxGainDb);
                    b.q = (float) std::clamp ((double) b.q, o.minQ, o.maxQ);
                };

                auto evaluate = [&] (BandSettings& b)
                {
                    clampBand (b);
                    bandCurve (b, hz, sampleRate, curve);
                    return squaredError (residual, curve);
                };

                auto best = evaluate (s);
                std::array<double, 3> step { 0.25, 1.0, 0.5 }; // octaves, dB, octaves of Q

                for (int iteration = 0; iteration < 80; ++iteration)
                {
                    auto improved = false;

                    for (int p = 0; p < 3; ++p)
                    {
                        for (auto direction : { 1.0, -1.0 })
                        {
                            auto trial = s;
                            const auto delta = direction * step[(size_t) p];

                            if (p == 0)      trial.frequency = (float) (trial.frequency * std::pow (2.0, delta));
                            else if (p == 1) trial.gainDb = (float) (trial.gainDb + delta);
                            else             trial.q = (float) (trial.q * std::pow (2.0, delta));

                            const auto error = evaluate (trial);

                            if (error < best)
                            {
                                best = error;
                                s = trial;
                                improved = true;
                                break;
                            }
                        }
                    }

                    if (! improved)
                    {
                        for (auto& st : step)
                            st *= 0.5;

                        if (step[1] < 0.01)
                            break;
                    }
                }

                return { s, best };
            }

            // Where the residual falls to half the peak on each side, or -1
            // when it never does before the end of the spectrum.
            inline std::pair<int, int> halfCrossings (const std::vector<double>& r, int peak)
            {
                const auto half = 0.5 * r[(size_t) peak];
                const auto beyond = [&] (int i) { return half > 0.0 ? r[(size_t) i] <= half : r[(size_t) i] >= half; };

                auto low = -1, high = -1;
                for (int i = peak; i >= 0; --i)
                    if (beyond (i)) { low = i; break; }

                for (int i = peak; i < (int) r.size(); ++i)
                    if (beyond (i)) { high = i; break; }

                return { low, high };
            }
        }

        // Bands (bells and shelves, enabled, stereo) that add up to the
        // target curve. Returns fewer than maxBands when the curve is
        // already matched to within the tolerance.
        inline std::vector<BandSettings> fit (const std::vector<double>& hz, const std::vector<double>& target,
                                              double sampleRate, const Options& o = {})
        {
            std::vector<BandSettings> result;
            if (hz.size() < 3 || hz.size() != target.size() || o.maxBands <= 0)
                return result;

            auto residual = target;
            std::vector<double> curve;
            const std::vector<double> zero (hz.size(), 0.0);

            auto subtract = [&] (const BandSettings& b, double sign)
            {
                bandCurve (b, hz, sampleRate, curve);
                for (size_t i = 0; i < hz.size(); ++i)
                    residual[i] -= sign * curve[i];
            };

            for (int n = 0; n < o.maxBands; ++n)
            {
                auto peak = 0;
                for (int i = 1; i < (int) hz.size(); ++i)
                    if (std::abs (residual[(size_t) i]) > std::abs (residual[(size_t) peak]))
                        peak = i;

                if (std::abs (residual[(size_t) peak]) < o.toleranceDb)
                    break;

                const auto [low, high] = detail::halfCrossings (residual, peak);

                BandSettings bell;
                bell.enabled = true;
                bell.type = FilterType::bell;
                bell.frequency = (float) hz[(size_t) peak];
                bell.gainDb = (float) residual[(size_t) peak];

                // Q from the width at half height: a bell's -half-gain
                // bandwidth in octaves is roughly 1 / Q.
                const auto lowHz = low >= 0 ? hz[(size_t) low] : hz.front();
                const auto highHz = high >= 0 ? hz[(size_t) high] : hz.back();
                bell.q = (float) (1.0 / std::max (0.1, std::log2 (highHz / lowHz)));

                std::vector<BandSettings> candidates { bell };

                if (low < 0)
                {
                    auto shelf = bell;
                    shelf.type = FilterType::lowShelf;
                    shelf.frequency = (float) (high >= 0 ? hz[(size_t) high] : hz[(size_t) peak]);
                    shelf.q = 0.7f;
                    candidates.push_back (shelf);
                }

                if (high < 0)
                {
                    auto shelf = bell;
                    shelf.type = FilterType::highShelf;
                    shelf.frequency = (float) (low >= 0 ? hz[(size_t) low] : hz[(size_t) peak]);
                    shelf.q = 0.7f;
                    candidates.push_back (shelf);
                }

                detail::Candidate best { {}, detail::squaredError (residual, zero) };
                auto found = false;

                for (const auto& c : candidates)
                {
                    const auto tuned = detail::tune (c, hz, residual, sampleRate, o);
                    // A band has to earn its place.
                    if (tuned.error < best.error * 0.97)
                    {
                        best = tuned;
                        found = true;
                    }
                }

                if (! found)
                    break;

                subtract (best.band, 1.0);
                result.push_back (best.band);
            }

            // Retune each band with all the others in place.
            for (int pass = 0; pass < 2; ++pass)
            {
                for (auto& b : result)
                {
                    subtract (b, -1.0);
                    b = detail::tune (b, hz, residual, sampleRate, o).band;
                    subtract (b, 1.0);
                }
            }

            // Drop bands that ended up doing nothing.
            result.erase (std::remove_if (result.begin(), result.end(),
                                          [] (const BandSettings& b) { return std::abs (b.gainDb) < 0.25f; }),
                          result.end());

            return result;
        }
    }
}
