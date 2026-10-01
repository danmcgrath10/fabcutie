#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

#include "EqTypes.h"

namespace fabcutie::dsp
{
    // Every band is built from trapezoidal-integrated state variable filters
    // (Zavalishin's TPT structure, in the form popularised by Andrew Simper).
    // They stay stable and smooth while frequency, gain and Q are modulated,
    // which plain direct-form biquads do not.
    //
    // One section computes   y = m0 * x + m1 * band + m2 * low
    // where low and band are the 2-pole lowpass and bandpass outputs, so its
    // analog prototype is    H(s) = m0 + (m1 * s + m2) / (s^2 + k * s + 1),
    // with s normalised to the section's prewarped cutoff g = tan(pi f / fs).
    struct SvfSection
    {
        double g = 0.0, k = 1.0;
        double m0 = 1.0, m1 = 0.0, m2 = 0.0;
    };

    // A first-order section (used for odd cut orders): lowpass 1 / (s + 1) or
    // highpass s / (s + 1).
    struct OnePoleSection
    {
        double g = 0.0;
        bool highpass = false;
    };

    struct BandDesign
    {
        static constexpr int maxSections = maxFilterOrder / 2;

        std::array<SvfSection, maxSections> sections {};
        int numSections = 0;

        bool hasOnePole = false;
        OnePoleSection onePole {};
    };

    // Q of each 2-pole section of an order-n Butterworth filter. A real pole
    // (odd orders) is not included.
    inline double butterworthSectionQ (int order, int section) noexcept
    {
        const auto pi = 3.14159265358979323846;

        if (order % 2 == 0)
            return 1.0 / (2.0 * std::cos ((2.0 * section + 1.0) * pi / (2.0 * order)));

        return 1.0 / (2.0 * std::cos ((section + 1.0) * pi / order));
    }

    // A section with any second-order numerator over the SVF denominator,
    // H(s) = (b2 s^2 + b1 s + b0) / (s^2 + k s + 1), rewritten in the
    // m0 + (m1 s + m2) / (...) form the processor runs.
    inline SvfSection svfFromBiquad (double g, double k, double b2, double b1, double b0) noexcept
    {
        return { g, k, b2, b1 - b2 * k, b0 - b2 };
    }

    // Brickwall cut: an order-16 inverse Chebyshev (Chebyshev type II) filter.
    // Its pass band is monotonic like a Butterworth's, and pairs of zeros on
    // the j axis just past the cutoff give a much faster drop, with a
    // 100 dB stop band. The prototype is scaled so the cutoff is its -3 dB
    // point, matching the other slopes.
    inline void designBrickwall (BandDesign& d, double g, bool highpass) noexcept
    {
        const auto pi = 3.14159265358979323846;
        constexpr int order = maxFilterOrder;
        constexpr double stopBandDb = 100.0;

        const auto invEps = std::sqrt (std::pow (10.0, stopBandDb / 10.0) - 1.0); // 1 / epsilon
        const auto mu = std::asinh (invEps) / order;
        const auto w3 = 1.0 / std::cosh (std::acosh (invEps) / order); // -3 dB point of the raw prototype

        for (int i = 0; i < order / 2; ++i)
        {
            const auto theta = (2.0 * i + 1.0) * pi / (2.0 * order);

            // Inverse of the matching Chebyshev type I pole, rescaled to w3 = 1.
            const std::complex<double> chebyPole { -std::sinh (mu) * std::sin (theta), std::cosh (mu) * std::cos (theta) };
            const auto pole = 1.0 / (chebyPole * w3);
            const auto zero = 1.0 / (std::cos (theta) * w3);

            const auto radius = std::abs (pole);
            const auto k = -2.0 * pole.real() / radius;
            const auto r = zero / radius; // zero frequency relative to the section's

            // Lowpass section with unity gain at DC; the highpass is its
            // s -> 1/s mirror with unity gain at the top.
            if (highpass)
                d.sections[(size_t) d.numSections++] = svfFromBiquad (g / radius, k, 1.0, 0.0, 1.0 / (r * r));
            else
                d.sections[(size_t) d.numSections++] = svfFromBiquad (g * radius, k, 1.0 / (r * r), 0.0, 1.0);
        }
    }

    inline std::complex<double> designResponse (const BandDesign&, double frequency, double sampleRate) noexcept;

    // Flat tilt: a constant dB-per-octave slope through the band frequency.
    // The gain is the level difference it makes across 20 Hz to 20 kHz.
    // A straight line in dB needs a "half-order" filter, so it is built the
    // classic way: 16 first-order pole/zero pairs spaced evenly in log
    // frequency, each making a small step, two pairs per section.
    //
    // The pairs are spaced in the bilinear-warped frequency the filters
    // really run at, from 4 Hz to past Nyquist. Near Nyquist that warp
    // stretches the octaves, and each pair's step blurs into its neighbours,
    // so the step sizes are refined a few times against the target line.
    // Finally the result is scaled to 0 dB at the band frequency.
    inline constexpr double flatTiltOctaves = 9.965784284662087; // log2 (20000 / 20)

    inline void designFlatTilt (BandDesign& d, double gainDb, double frequency, double sampleRate) noexcept
    {
        const auto pi = 3.14159265358979323846;
        constexpr int pairs = 2 * BandDesign::maxSections;
        constexpr int refinements = 3;

        const auto warp   = [&] (double hz) { return std::tan (pi * hz / sampleRate); };
        const auto unwarp = [&] (double w)  { return std::atan (w) * sampleRate / pi; };

        const auto lowW  = warp (4.0);
        const auto highW = 4.0 * warp (std::min (24000.0, 0.45 * sampleRate));
        const auto topHz = 0.45 * sampleRate; // the line is matched up to here
        const auto spacing = std::log2 (highW / lowW) / pairs; // warped octaves per pair
        const auto slope = gainDb / flatTiltOctaves;            // dB per octave (in Hz)

        std::array<double, pairs> centre {}, step {};

        for (int i = 0; i < pairs; ++i)
        {
            centre[(size_t) i] = lowW * std::exp2 (spacing * (i + 0.5));

            // First guess: the line's slope per warped octave, which is the slope
            // in Hz divided by d log2(warped) / d log2(Hz) = x / (sin x cos x).
            const auto x = std::atan (centre[(size_t) i]);
            step[(size_t) i] = slope * spacing * std::sin (x) * std::cos (x) / x;
        }

        const auto build = [&]
        {
            d.numSections = 0;

            for (int i = 0; i < pairs; i += 2)
            {
                double poles[2], zeros[2];

                for (int j = 0; j < 2; ++j)
                {
                    // A pole and zero split octaves apart make a 6.02 * split dB step;
                    // the zero comes first for a rise.
                    const auto split = std::abs (step[(size_t) (i + j)]) / 6.0206;
                    const auto below = centre[(size_t) (i + j)] * std::exp2 (-0.5 * split);
                    const auto above = centre[(size_t) (i + j)] * std::exp2 (0.5 * split);

                    zeros[j] = step[(size_t) (i + j)] >= 0.0 ? below : above;
                    poles[j] = step[(size_t) (i + j)] >= 0.0 ? above : below;
                }

                // (s/z1 + 1)(s/z2 + 1) / ((s/p1 + 1)(s/p2 + 1)), normalised to
                // the poles' geometric mean so the denominator is s^2 + k s + 1.
                const auto g = std::sqrt (poles[0] * poles[1]);
                const auto a = poles[0] / g;
                const auto c1 = g / zeros[0], c2 = g / zeros[1];

                d.sections[(size_t) d.numSections++] = svfFromBiquad (g, a + 1.0 / a, c1 * c2, c1 + c2, 1.0);
            }
        };

        for (int pass = 0; pass < refinements; ++pass)
        {
            build();

            // Error against the line at the cell edges; each step absorbs the
            // change in error across its own cell. Above the matched range the
            // error is held, so those steps are left alone.
            std::array<double, pairs + 1> error {};

            for (int n = 0; n <= pairs; ++n)
            {
                const auto hz = unwarp (lowW * std::exp2 (spacing * n));

                if (hz > topHz)
                {
                    error[(size_t) n] = error[(size_t) n - 1];
                    continue;
                }

                const auto db = 20.0 * std::log10 (std::abs (designResponse (d, hz, sampleRate)));
                error[(size_t) n] = db - slope * std::log2 (hz);
            }

            for (int i = 0; i < pairs; ++i)
                step[(size_t) i] -= error[(size_t) i + 1] - error[(size_t) i];
        }

        build();

        const auto pivot = std::abs (designResponse (d, frequency, sampleRate));
        auto& first = d.sections[0];
        first.m0 /= pivot;
        first.m1 /= pivot;
        first.m2 /= pivot;
    }

    inline BandDesign designBand (const BandSettings& s, double sampleRate) noexcept
    {
        const auto pi = 3.14159265358979323846;

        // Keep the cutoff below Nyquist so tan() stays finite.
        const auto freq = std::clamp ((double) s.frequency, 5.0, 0.49 * sampleRate);
        const auto g    = std::tan (pi * freq / sampleRate);
        const auto q    = std::max (0.01, (double) s.q);
        const auto A    = std::pow (10.0, (double) s.gainDb / 40.0); // sqrt of linear gain

        BandDesign d;

        auto addSection = [&d] (double sg, double k, double m0, double m1, double m2)
        {
            d.sections[(size_t) d.numSections++] = { sg, k, m0, m1, m2 };
        };

        switch (s.type)
        {
            case FilterType::bell:
            {
                // k scaled by 1/A keeps boosts and cuts of the same Q mirror images.
                const auto k = 1.0 / (q * A);
                addSection (g, k, 1.0, k * (A * A - 1.0), 0.0);
                break;
            }

            case FilterType::lowShelf:
            {
                const auto k = 1.0 / q;
                addSection (g / std::sqrt (A), k, 1.0, k * (A - 1.0), A * A - 1.0);
                break;
            }

            case FilterType::highShelf:
            {
                const auto k = 1.0 / q;
                addSection (g * std::sqrt (A), k, A * A, k * (1.0 - A) * A, 1.0 - A * A);
                break;
            }

            case FilterType::tiltShelf:
            {
                // A high shelf with the full gain, pulled down by half of it:
                // lows go to -gain/2, highs to +gain/2, pivoting at the frequency.
                const auto k = 1.0 / q;
                addSection (g * std::sqrt (A), k, A, k * (1.0 - A), (1.0 - A * A) / A);
                break;
            }

            case FilterType::notch:
            {
                const auto k = 1.0 / q;
                addSection (g, k, 1.0, -k, 0.0);
                break;
            }

            case FilterType::bandPass:
            {
                const auto k = 1.0 / q;
                addSection (g, k, 0.0, k, 0.0);
                break;
            }

            case FilterType::allPass:
            {
                // (s^2 - k s + 1) / (s^2 + k s + 1): unity magnitude, with the
                // phase turning through 360 degrees, fastest at high Q.
                const auto k = 1.0 / q;
                addSection (g, k, 1.0, -2.0 * k, 0.0);
                break;
            }

            case FilterType::flatTilt:
                designFlatTilt (d, s.gainDb, freq, sampleRate);
                break;

            case FilterType::lowCut:
            case FilterType::highCut:
            {
                const auto highpass = s.type == FilterType::lowCut;

                if (isBrickwall (s.slopeIndex))
                {
                    designBrickwall (d, g, highpass);
                    break;
                }

                const auto order = filterOrderForSlope (s.slopeIndex);
                const auto pairs = order / 2;

                if (order % 2 == 1)
                {
                    d.hasOnePole = true;
                    d.onePole = { g, highpass };
                }

                for (int i = 0; i < pairs; ++i)
                {
                    auto sectionQ = butterworthSectionQ (order, i);

                    // Q = 1 gives a flat Butterworth knee; the user's Q scales the
                    // resonance of the sharpest section.
                    if (i == pairs - 1)
                        sectionQ *= q;

                    const auto k = 1.0 / sectionQ;

                    if (highpass)
                        addSection (g, k, 1.0, -k, -1.0);
                    else
                        addSection (g, k, 0.0, 0.0, 1.0);
                }
                break;
            }
        }

        return d;
    }

    // Exact frequency response of a design as the processor runs it. The
    // trapezoidal filters are the bilinear transform of their analog
    // prototypes, so each section is evaluated at s = j tan(w / 2) / g.
    inline std::complex<double> designResponse (const BandDesign& d, double frequency, double sampleRate) noexcept
    {
        const auto pi = 3.14159265358979323846;
        const auto warped = std::tan (pi * std::min (frequency, 0.4999 * sampleRate) / sampleRate);

        std::complex<double> h { 1.0, 0.0 };

        for (int i = 0; i < d.numSections; ++i)
        {
            const auto& sec = d.sections[(size_t) i];
            const std::complex<double> s { 0.0, warped / sec.g };
            h *= sec.m0 + (sec.m1 * s + sec.m2) / (s * s + sec.k * s + 1.0);
        }

        if (d.hasOnePole)
        {
            const std::complex<double> s { 0.0, warped / d.onePole.g };
            h *= (d.onePole.highpass ? s : std::complex<double> { 1.0, 0.0 }) / (s + 1.0);
        }

        return h;
    }

    // Magnitude response of a band in dB, e.g. for drawing the EQ curve.
    // Disabled bands are flat.
    inline double bandMagnitudeDb (const BandSettings& s, double frequency, double sampleRate) noexcept
    {
        if (! s.enabled)
            return 0.0;

        const auto mag = std::abs (designResponse (designBand (s, sampleRate), frequency, sampleRate));
        return 20.0 * std::log10 (std::max (mag, 1.0e-30));
    }
}
