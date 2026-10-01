// Offline checks for the EQ engine: measured sine responses must match the
// analytic curve, slopes and placements must behave, and the filters must
// stay finite under heavy parameter automation.

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "dsp/EqEngine.h"

using namespace fabcutie::dsp;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok)
        {
            ++failures;
            std::printf ("FAIL: %s\n", what.c_str());
        }
    }

    // Runs a sine through the engine (left and right fed by the given gains)
    // and returns the steady-state RMS gain in dB of each output channel.
    std::pair<double, double> measure (EqEngine& eq, double freq, float leftIn = 1.0f, float rightIn = 1.0f)
    {
        const auto settle = (int) sampleRate;        // let smoothing and transients finish
        const auto length = (int) (sampleRate / 2);  // then measure half a second

        juce::AudioBuffer<float> buffer (2, blockSize);
        double phase = 0.0, inSum = 0.0, outL = 0.0, outR = 0.0;
        const auto inc = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;

        for (int done = 0; done < settle + length; done += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = (float) std::sin (phase);
                phase = std::fmod (phase + inc, 2.0 * juce::MathConstants<double>::pi);
                buffer.setSample (0, i, leftIn * x);
                buffer.setSample (1, i, rightIn * x);
                if (done >= settle) inSum += (double) x * x;
            }

            eq.process (buffer);

            if (done >= settle)
                for (int i = 0; i < blockSize; ++i)
                {
                    outL += juce::square ((double) buffer.getSample (0, i));
                    outR += juce::square ((double) buffer.getSample (1, i));
                }
        }

        auto toDb = [inSum] (double sum, float in)
        {
            if (juce::exactlyEqual (in, 0.0f)) return sum > 0.0 ? 10.0 * std::log10 (sum / inSum) : -300.0;
            return 10.0 * std::log10 (std::max (sum, 1.0e-30) / (inSum * in * in));
        };

        return { toDb (outL, leftIn), toDb (outR, rightIn) };
    }

    EqEngine makeEngine (const BandSettings& band, int index = 0)
    {
        EqEngine eq;
        eq.setBand (index, band);
        eq.prepare (sampleRate);
        return eq;
    }

    BandSettings band (FilterType type, float freq, float gain, float q, int slope = 1, Placement p = Placement::stereo)
    {
        BandSettings s;
        s.enabled = true;
        s.type = type;
        s.frequency = freq;
        s.gainDb = gain;
        s.q = q;
        s.slopeIndex = slope;
        s.placement = p;
        return s;
    }

    std::string describe (const BandSettings& s, double f)
    {
        return "type " + std::to_string ((int) s.type) + " slope " + std::to_string (s.slopeIndex)
             + " fc " + std::to_string (s.frequency) + " gain " + std::to_string (s.gainDb)
             + " q " + std::to_string (s.q) + " at " + std::to_string (f) + " Hz";
    }

    void testPassThrough()
    {
        EqEngine eq;
        eq.prepare (sampleRate);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::Random rng (1);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (c, i, rng.nextFloat() * 2.0f - 1.0f);

        juce::AudioBuffer<float> copy (buffer);
        eq.process (buffer);

        auto same = true;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
                same = same && juce::exactlyEqual (buffer.getSample (c, i), copy.getSample (c, i));

        check (same, "all bands off is a bit-exact pass-through");
    }

    // Every shape and slope: the measured sine gain matches the analytic curve.
    void testResponseMatchesAnalytic()
    {
        const std::vector<BandSettings> cases {
            band (FilterType::bell, 1000, 12, 1),
            band (FilterType::bell, 1000, -18, 4),
            band (FilterType::bell, 60, 6, 0.3f),
            band (FilterType::lowShelf, 200, 9, 0.7f),
            band (FilterType::highShelf, 5000, -12, 1.5f),
            band (FilterType::tiltShelf, 1000, 6, 0.7f),
            band (FilterType::notch, 3000, 0, 2),
            band (FilterType::bandPass, 800, 0, 1),
            band (FilterType::lowCut, 100, 0, 1, 0),
            band (FilterType::lowCut, 100, 0, 1, 2),
            band (FilterType::lowCut, 300, 0, 4, 3),
            band (FilterType::highCut, 2000, 0, 1, 5),
            band (FilterType::highCut, 2000, 0, 1, 8),
            band (FilterType::highCut, 12000, 0, 0.5f, 4),
        };

        const double freqs[] { 40, 100, 250, 700, 1000, 1500, 2600, 4000, 9000, 15000 };

        for (const auto& s : cases)
            for (auto f : freqs)
            {
                const auto expected = bandMagnitudeDb (s, f, sampleRate);
                if (expected < -70.0) continue; // too far down to measure against float noise

                auto eq = makeEngine (s);
                const auto [left, right] = measure (eq, f);
                check (std::abs (left - expected) < 0.1 && std::abs (right - expected) < 0.1,
                       describe (s, f) + ": measured " + std::to_string (left) + " expected " + std::to_string (expected));
            }
    }

    void testShapes()
    {
        auto db = [] (const BandSettings& s, double f) { return bandMagnitudeDb (s, f, sampleRate); };

        const auto bell = band (FilterType::bell, 1000, 12, 1);
        check (std::abs (db (bell, 1000) - 12.0) < 0.01, "bell hits its gain at the centre");
        check (std::abs (db (bell, 20)) < 0.1, "bell is flat far below");

        const auto ls = band (FilterType::lowShelf, 200, 9, 0.7071f);
        check (std::abs (db (ls, 20) - 9.0) < 0.1 && std::abs (db (ls, 10000)) < 0.1, "low shelf plateaus");
        check (std::abs (db (ls, 200) - 4.5) < 0.05, "low shelf is at half gain at its frequency");

        const auto tilt = band (FilterType::tiltShelf, 1000, 6, 0.7071f);
        check (std::abs (db (tilt, 20) + 3.0) < 0.1 && std::abs (db (tilt, 20000) - 3.0) < 0.2, "tilt goes -g/2 to +g/2");

        check (db (band (FilterType::notch, 3000, 0, 2), 3000) < -100.0, "notch is deep at its centre");
        check (std::abs (db (band (FilterType::bandPass, 800, 0, 1), 800)) < 0.01, "band pass peaks at unity");

        for (int slope = 0; slope < (int) cutSlopesDbPerOct.size(); ++slope)
        {
            const auto hc = band (FilterType::highCut, 1000, 0, 1, slope);
            const auto lc = band (FilterType::lowCut, 1000, 0, 1, slope);
            const auto name = std::to_string (cutSlopesDbPerOct[(size_t) slope]) + " dB/oct";

            check (std::abs (db (hc, 1000) + 3.01) < 0.05, name + " high cut is -3 dB at the cutoff");
            check (std::abs (db (lc, 1000) + 3.01) < 0.05, name + " low cut is -3 dB at the cutoff");

            // Two to three octaves out, the roll-off per octave approaches the slope.
            const auto lcSlope = db (lc, 250) - db (lc, 125);
            check (std::abs (lcSlope - cutSlopesDbPerOct[(size_t) slope]) < 0.5,
                   name + " low cut slope measured " + std::to_string (lcSlope));

            check (std::abs (db (hc, 100)) < 0.05, name + " high cut is flat a decade into the pass band");
        }

        check (db (band (FilterType::highCut, 1000, 0, 1, 8), 2000) < -95.0, "96 dB/oct high cut is ~96 dB down an octave above");
    }

    void testPlacement()
    {
        const auto mid = band (FilterType::bell, 1000, 12, 1, 1, Placement::mid);

        {
            auto eq = makeEngine (mid);
            const auto [l, r] = measure (eq, 1000, 1.0f, 1.0f); // pure mid
            check (std::abs (l - 12.0) < 0.1 && std::abs (r - 12.0) < 0.1, "mid band boosts a mid signal");
        }
        {
            auto eq = makeEngine (mid);
            const auto [l, r] = measure (eq, 1000, 1.0f, -1.0f); // pure side
            check (std::abs (l) < 0.01 && std::abs (r) < 0.01, "mid band leaves a side signal alone");
        }
        {
            auto eq = makeEngine (band (FilterType::bell, 1000, 12, 1, 1, Placement::side));
            const auto [l, r] = measure (eq, 1000, 1.0f, -1.0f);
            check (std::abs (l - 12.0) < 0.1 && std::abs (r - 12.0) < 0.1, "side band boosts a side signal");
        }
        {
            auto eq = makeEngine (band (FilterType::bell, 1000, -12, 1, 1, Placement::left));
            const auto [l, r] = measure (eq, 1000);
            check (std::abs (l + 12.0) < 0.1 && juce::exactlyEqual (r, 0.0), "left band only touches the left channel");
        }
        {
            auto eq = makeEngine (band (FilterType::bell, 1000, -12, 1, 1, Placement::right));
            const auto [l, r] = measure (eq, 1000);
            check (juce::exactlyEqual (l, 0.0) && std::abs (r + 12.0) < 0.1, "right band only touches the right channel");
        }
        {
            // Mixed placements in one engine: mid boost then side cut.
            EqEngine eq;
            eq.setBand (0, mid);
            eq.setBand (5, band (FilterType::bell, 1000, -6, 1, 1, Placement::side));
            eq.prepare (sampleRate);
            const auto [l, r] = measure (eq, 1000, 1.0f, 0.0f); // half mid, half side
            const auto expected = 20.0 * std::log10 (0.5 * std::pow (10.0, 12.0 / 20.0) + 0.5 * std::pow (10.0, -6.0 / 20.0));
            check (std::abs (l - expected) < 0.1, "mid and side bands combine on the left channel");
        }
    }

    // Toggling a band or swapping its type must not jump the output.
    void testSmoothSwitching()
    {
        EqEngine eq;
        auto s = band (FilterType::bell, 100, 30, 1);
        s.enabled = false;
        eq.setBand (0, s);
        eq.prepare (sampleRate);

        juce::AudioBuffer<float> buffer (2, 64);
        double phase = 0.0;
        float last = 0.0f, maxStep = 0.0f;
        const auto inc = 2.0 * juce::MathConstants<double>::pi * 100.0 / sampleRate;

        for (int block = 0; block < 3000; ++block)
        {
            if (block % 300 == 150)
            {
                s.enabled = ! s.enabled;
                s.type = (block / 300) % 2 == 0 ? FilterType::bell : FilterType::lowShelf;
                eq.setBand (0, s);
            }

            for (int i = 0; i < 64; ++i)
            {
                const auto x = 0.03f * (float) std::sin (phase);
                phase += inc;
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }

            eq.process (buffer);

            for (int i = 0; i < 64; ++i)
            {
                maxStep = std::max (maxStep, std::abs (buffer.getSample (0, i) - last));
                last = buffer.getSample (0, i);
            }
        }

        // A full-scale +30 dB sine at 100 Hz moves at most ~0.0124 per sample;
        // a hard switch would jump by up to ~0.9.
        check (maxStep < 0.02f, "switching bands does not click (max step " + std::to_string (maxStep) + ")");
    }

    void testAutomationStability()
    {
        EqEngine eq;
        eq.prepare (sampleRate);

        std::mt19937 rng (42);
        std::uniform_real_distribution<float> uni (0.0f, 1.0f);

        juce::AudioBuffer<float> buffer (2, 128);
        auto finite = true;
        float peak = 0.0f;

        for (int block = 0; block < 20000; ++block)
        {
            for (int b = 0; b < maxBands; ++b)
            {
                if (uni (rng) > 0.3f) continue;

                BandSettings s;
                s.enabled = uni (rng) > 0.2f;
                s.type = (FilterType) (int) (uni (rng) * numFilterTypes * 0.999f);
                s.frequency = 10.0f * std::pow (3000.0f, uni (rng));
                s.gainDb = -30.0f + 60.0f * uni (rng) * 0.4f; // keep summed boosts sane
                s.q = 0.025f * std::pow (1600.0f, uni (rng));
                s.slopeIndex = (int) (uni (rng) * 8.999f);
                s.placement = (Placement) (int) (uni (rng) * numPlacements * 0.999f);
                eq.setBand (b, s);
            }

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 128; ++i)
                    buffer.setSample (c, i, 0.1f * (uni (rng) * 2.0f - 1.0f));

            eq.process (buffer);

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 128; ++i)
                {
                    const auto y = buffer.getSample (c, i);
                    finite = finite && std::isfinite (y);
                    peak = std::max (peak, std::abs (y));
                }
        }

        check (finite, "random automation keeps the output finite");
        std::printf ("automation stress peak: %.2f\n", peak);
    }

    void testMono()
    {
        EqEngine eq;
        eq.setBand (0, band (FilterType::bell, 1000, 12, 1, 1, Placement::side));
        eq.setBand (1, band (FilterType::bell, 1000, -6, 1, 1, Placement::mid));
        eq.prepare (sampleRate);

        juce::AudioBuffer<float> buffer (1, blockSize);
        double phase = 0.0, in = 0.0, out = 0.0;
        for (int block = 0; block < 200; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                buffer.setSample (0, i, (float) std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;
            }
            eq.process (buffer);
            if (block >= 100)
                for (int i = 0; i < blockSize; ++i)
                {
                    in += 0.5;
                    out += juce::square ((double) buffer.getSample (0, i));
                }
        }

        check (std::abs (10.0 * std::log10 (out / in) + 6.0) < 0.1, "mono: mid bands apply, side bands do not");
    }
}

int main()
{
    testPassThrough();
    testShapes();
    testResponseMatchesAnalytic();
    testPlacement();
    testMono();
    testSmoothSwitching();
    testAutomationStability();

    if (failures == 0)
        std::printf ("All EQ engine tests passed.\n");
    else
        std::printf ("%d EQ engine test(s) failed.\n", failures);

    return failures == 0 ? 0 : 1;
}
