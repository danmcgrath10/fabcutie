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

            case FilterType::lowCut:
            case FilterType::highCut:
            {
                const auto highpass = s.type == FilterType::lowCut;
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
