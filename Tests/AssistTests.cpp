// Offline checks for Assist: the key bus that carries one instance's output
// to another (auto-unmasking), and the analysis that finds resonances and
// collisions.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "dsp/Assist.h"
#include "dsp/CurveFit.h"
#include "dsp/EqEngine.h"
#include "dsp/KeyBus.h"
#include "dsp/SpectrumMatch.h"

using namespace fabcutie::dsp;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (std::getenv ("FABCUTIE_VERBOSE") != nullptr)
            std::printf ("%s: %s\n", ok ? "ok" : "--", what.c_str());

        if (! ok)
        {
            ++failures;
            std::printf ("FAIL: %s\n", what.c_str());
        }
    }

    std::string fmt (double v)
    {
        char text[32];
        std::snprintf (text, sizeof (text), "%.2f", v);
        return text;
    }

    double octavesApart (double a, double b) { return std::abs (std::log2 (a / b)); }

    // Writes a counting ramp in blocks and checks the reader sees it without
    // gaps or repeats once it has caught on, whichever runs first each cycle.
    void testKeyBusContinuity (bool readerFirst)
    {
        KeyBus bus;
        KeyBus::Reader reader;
        std::vector<float> in ((size_t) blockSize), outL ((size_t) blockSize), outR ((size_t) blockSize);
        float* out[] { outL.data(), outR.data() };
        const float* inPtr[] { in.data() };

        float next = 0.0f, expected = -1.0f;
        auto continuous = true;

        for (int cycle = 0; cycle < 200; ++cycle)
        {
            auto write = [&]
            {
                for (auto& v : in) v = next++;
                bus.write (inPtr, 1, blockSize);
            };

            if (! readerFirst)
                write();

            const auto got = bus.read (reader, out, 2, blockSize);

            if (got && cycle > 2)
            {
                if (expected >= 0.0f && ! juce::exactlyEqual (outL[0], expected))
                    continuous = false;

                for (int i = 1; i < blockSize; ++i)
                    if (! juce::exactlyEqual (outL[(size_t) i], outL[(size_t) i - 1] + 1.0f) || ! juce::exactlyEqual (outR[(size_t) i], outL[(size_t) i]))
                        continuous = false;
            }

            if (got)
                expected = outL[(size_t) blockSize - 1] + 1.0f;

            if (readerFirst)
                write();
        }

        check (continuous, std::string ("key bus: reader follows the writer without gaps (")
                               + (readerFirst ? "reader first" : "writer first") + ")");
    }

    void testKeyBusStops()
    {
        KeyBus bus;
        KeyBus::Reader reader;
        std::vector<float> in ((size_t) blockSize, 0.5f), out ((size_t) blockSize);
        float* outPtr[] { out.data() };
        const float* inPtr[] { in.data() };

        bus.write (inPtr, 1, blockSize);
        bus.write (inPtr, 1, blockSize);

        check (bus.read (reader, outPtr, 1, blockSize) && juce::exactlyEqual (out[0], 0.5f), "key bus: reads what was written");

        // The key stops processing: a few repeats, then silence.
        auto stillGoing = 0;
        for (int i = 0; i < 20; ++i)
            if (bus.read (reader, outPtr, 1, blockSize))
                ++stillGoing;

        check (stillGoing <= KeyBus::maxIdleBlocks + 1 && juce::exactlyEqual (out[0], 0.0f),
               "key bus: a key that stops goes silent (" + std::to_string (stillGoing) + " repeats)");

        // And starts again.
        bus.write (inPtr, 1, blockSize);
        check (bus.read (reader, outPtr, 1, blockSize) && juce::exactlyEqual (out[0], 0.5f), "key bus: a key that restarts is heard again");

        KeyBus::Reader fresh;
        KeyBus empty;
        check (! empty.read (fresh, outPtr, 1, blockSize) && juce::exactlyEqual (out[0], 0.0f), "key bus: nothing written reads silence");
    }

    void testKeyBusPool()
    {
        KeyBusPool pool;
        auto* a = pool.claim (11);
        auto* b = pool.claim (22);

        check (a != nullptr && b != nullptr && a != b, "pool: claims separate buses");
        check (pool.find (11) == a && pool.find (22) == b && pool.find (33) == nullptr && pool.find (0) == nullptr, "pool: finds buses by ID");

        pool.rename (a, 33);
        check (pool.find (11) == nullptr && pool.find (33) == a, "pool: rename moves the ID");

        pool.release (b);
        check (pool.find (22) == nullptr, "pool: release forgets the ID");

        auto* c = pool.claim (44);
        check (c == b, "pool: a released slot is reused");
    }

    // A loud key in the band's region pulls an External dynamic band down
    // by its range when the key reaches it through the bus.
    void testUnmaskDucking()
    {
        KeyBus bus;
        KeyBus::Reader reader;

        EqEngine eq;
        eq.prepare (sampleRate);

        BandSettings band;
        band.enabled = true;
        band.type = FilterType::bell;
        band.frequency = 120.0f;
        band.q = 1.4f;
        band.dynamics.enabled = true;
        band.dynamics.thresholdDb = -30.0f;
        band.dynamics.rangeDb = -6.0f;
        band.dynamics.attackMs = 5.0f;
        band.dynamics.releaseMs = 100.0f;
        band.dynamics.source = DetectorSource::external;
        eq.setBand (0, band);

        juce::AudioBuffer<float> main (2, blockSize), key (2, blockSize);
        std::vector<float> kick ((size_t) blockSize);
        const float* kickPtr[] { kick.data() };
        double phase = 0.0;

        auto run = [&] (float keyAmplitude, int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                for (auto& v : kick)
                {
                    v = keyAmplitude * (float) std::sin (phase);
                    phase += twoPi * 120.0 / sampleRate;
                }

                bus.write (kickPtr, 1, blockSize);
                bus.read (reader, key.getArrayOfWritePointers(), 2, blockSize);

                main.clear();
                eq.process (main, &key);
            }
        };

        run (0.5f, 100);
        check (std::abs (eq.getDynamicGainDb (0) + 6.0f) < 0.1f, "unmask: a loud key ducks the band by its range ("
                                                                       + fmt (eq.getDynamicGainDb (0)) + " dB)");

        run (0.0f, 400);
        check (std::abs (eq.getDynamicGainDb (0)) < 0.1f, "unmask: the band recovers when the key stops ("
                                                              + fmt (eq.getDynamicGainDb (0)) + " dB)");
    }

    LongTermSpectrum learn (const std::vector<float>& x)
    {
        LongTermSpectrum spectrum;
        spectrum.push (x.data(), (int) x.size());
        return spectrum;
    }

    void testRangeLevel()
    {
        std::vector<float> x ((size_t) (sampleRate * 2));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.5f * (float) std::sin (twoPi * 1000.0 * (double) i / sampleRate);

        const auto spectrum = learn (x);
        const auto level = spectrum.rangeLevelDb (900.0, 1100.0, sampleRate);
        check (std::abs (level + 6.02) < 0.5, "range level: a 0.5 sine reads -6 dB (" + fmt (level) + ")");
        check (spectrum.rangeLevelDb (3000.0, 4000.0, sampleRate) < -60.0, "range level: nothing elsewhere");
    }

    void testResonances()
    {
        // White noise with two ringing tones in it, 12 dB or so above the
        // noise around them.
        std::mt19937 rng (7);
        std::normal_distribution<float> noise (0.0f, 0.1f);
        std::vector<float> x ((size_t) (sampleRate * 4));

        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto t = (double) i / sampleRate;
            x[i] = noise (rng) + 0.02f * (float) std::sin (twoPi * 350.0 * t) + 0.02f * (float) std::sin (twoPi * 3150.0 * t);
        }

        const auto spectrum = learn (x);
        const auto hz = curvefit::logFrequencies (20.0, 20000.0, 48);
        const auto found = assist::findResonances (hz, spectrum.levelsDb (hz, sampleRate, 1.0 / 24.0),
                                                   spectrum.levelsDb (hz, sampleRate, 1.0));

        auto near = [&] (double f)
        {
            for (const auto& s : found)
                if (octavesApart (s.frequency, f) < 1.0 / 24.0)
                    return true;
            return false;
        };

        check (found.size() == 2, "resonances: two found in noise (" + std::to_string (found.size()) + ")");
        check (near (350.0) && near (3150.0), "resonances: at the tones");

        for (const auto& s : found)
            check (s.q >= 2.0 && s.amountDb > 5.0, "resonances: narrow and standing out (Q " + fmt (s.q) + ", "
                                                       + fmt (s.amountDb) + " dB at " + fmt (s.frequency) + " Hz)");

        const auto clean = learn (std::vector<float> (x.size(), 0.0f));
        check (assist::findResonances (hz, clean.levelsDb (hz, sampleRate, 1.0 / 24.0), clean.levelsDb (hz, sampleRate, 1.0)).empty(),
               "resonances: none in silence");

        std::vector<float> justNoise (x.size());
        for (auto& v : justNoise) v = noise (rng);
        const auto flat = learn (justNoise);
        check (assist::findResonances (hz, flat.levelsDb (hz, sampleRate, 1.0 / 24.0), flat.levelsDb (hz, sampleRate, 1.0)).empty(),
               "resonances: none in plain noise");
    }

    void testCollisions()
    {
        // A bass-heavy track and a key that is loudest around 2 to 4 kHz but
        // also strong at 100 Hz (a kick against a bass, say).
        const auto hz = curvefit::logFrequencies (20.0, 20000.0, 12);
        std::vector<double> track (hz.size()), key (hz.size());

        for (size_t i = 0; i < hz.size(); ++i)
        {
            const auto o = std::log2 (hz[i]);
            track[i] = -20.0 - 6.0 * std::pow (o - std::log2 (110.0), 2.0);  // peaks at 110 Hz
            key[i] = std::max (-20.0 - 8.0 * std::pow (o - std::log2 (90.0), 2.0),     // kick body
                               -18.0 - 3.0 * std::pow (o - std::log2 (3000.0), 2.0)); // click
        }

        const auto found = assist::findCollisions (hz, track, key);
        check (! found.empty() && octavesApart (found.front().frequency, 100.0) < 0.4,
               "collisions: found where both are loud (" + (found.empty() ? std::string ("none") : fmt (found.front().frequency)) + " Hz)");
        check (found.size() == 1, "collisions: not where only one is loud (" + std::to_string (found.size()) + ")");

        if (! found.empty())
            check (found.front().q >= 0.7 && found.front().q <= 4.0, "collisions: a broad band (Q " + fmt (found.front().q) + ")");

        std::vector<double> apart (hz.size());
        for (size_t i = 0; i < hz.size(); ++i)
            apart[i] = -20.0 - 6.0 * std::pow (std::log2 (hz[i]) - std::log2 (6000.0), 2.0);

        check (assist::findCollisions (hz, track, apart).empty(), "collisions: none between tracks that do not overlap");
    }
}

int main()
{
    testKeyBusContinuity (false);
    testKeyBusContinuity (true);
    testKeyBusStops();
    testKeyBusPool();
    testUnmaskDucking();
    testRangeLevel();
    testResonances();
    testCollisions();

    if (failures == 0)
        std::printf ("All Assist tests passed\n");

    return failures == 0 ? 0 : 1;
}
