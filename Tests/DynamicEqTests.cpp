// Offline checks for dynamic EQ: the gain computer, steady-state gain at
// different levels, sidechain filtering, the external sidechain, attack and
// release timing, and stability under automation.

#include <cmath>
#include <cstdio>
#include <random>
#include <string>

#include "dsp/EqEngine.h"

using namespace fabcutie::dsp;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok)
        {
            ++failures;
            std::printf ("FAIL: %s\n", what.c_str());
        }
    }

    bool near (double value, double expected, double tolerance)
    {
        return std::abs (value - expected) <= tolerance;
    }

    std::string fmt (double v)
    {
        char text[32];
        std::snprintf (text, sizeof (text), "%.2f", v);
        return text;
    }

    BandSettings dynamicBell (float frequency = 1000.0f)
    {
        BandSettings s;
        s.enabled = true;
        s.type = FilterType::bell;
        s.frequency = frequency;
        s.q = 2.0f;
        s.gainDb = 0.0f;
        s.dynamics.enabled = true;
        s.dynamics.thresholdDb = -30.0f;
        s.dynamics.rangeDb = -12.0f;
        s.dynamics.attackMs = 5.0f;
        s.dynamics.releaseMs = 100.0f;
        return s;
    }

    struct Signal
    {
        double frequency = 1000.0;
        float amplitude = 1.0f;
    };

    struct Result
    {
        double gainDb = 0.0;        // measured main output gain, channel 0
        float dynamicGainDb = 0.0f; // the band's dynamic offset at the end
    };

    // Plays a sine into the main input (and optionally another into the
    // sidechain) for a second, then measures half a second of output.
    Result run (EqEngine& eq, Signal main, const Signal* sidechain = nullptr)
    {
        const auto settle = (int) sampleRate;
        const auto length = (int) (sampleRate / 2);

        juce::AudioBuffer<float> buffer (2, blockSize), side (2, blockSize);
        double phase = 0.0, sidePhase = 0.0, inSum = 0.0, outSum = 0.0;

        for (int done = 0; done < settle + length; done += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = main.amplitude * (float) std::sin (phase);
                phase = std::fmod (phase + twoPi * main.frequency / sampleRate, twoPi);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
                if (done >= settle) inSum += (double) x * x;

                if (sidechain != nullptr)
                {
                    const auto y = sidechain->amplitude * (float) std::sin (sidePhase);
                    sidePhase = std::fmod (sidePhase + twoPi * sidechain->frequency / sampleRate, twoPi);
                    side.setSample (0, i, y);
                    side.setSample (1, i, y);
                }
            }

            eq.process (buffer, sidechain != nullptr ? &side : nullptr);

            if (done >= settle)
                for (int i = 0; i < blockSize; ++i)
                    outSum += juce::square ((double) buffer.getSample (0, i));
        }

        return { 10.0 * std::log10 (std::max (outSum, 1.0e-30) / inSum), eq.getDynamicGainDb (0) };
    }

    EqEngine makeEngine (const BandSettings& band)
    {
        EqEngine eq;
        eq.setBand (0, band);
        eq.prepare (sampleRate);
        return eq;
    }

    float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

    void testGainComputer()
    {
        // Below the knee nothing happens; above it each dB over moves the gain
        // a dB, up to the range; the knee is continuous.
        check (juce::exactlyEqual (BandDynamics::gainOffsetDb (-50.0f, -30.0f, -12.0f), 0.0f), "gain computer: below threshold");
        check (near (BandDynamics::gainOffsetDb (-24.0f, -30.0f, -12.0f), -6.0, 1.0e-4), "gain computer: 6 dB over");
        check (near (BandDynamics::gainOffsetDb (0.0f, -30.0f, -12.0f), -12.0, 1.0e-4), "gain computer: clamps to range");
        check (near (BandDynamics::gainOffsetDb (0.0f, -30.0f, 8.0f), 8.0, 1.0e-4), "gain computer: positive range");
        check (near (BandDynamics::gainOffsetDb (-30.0f, -30.0f, -12.0f), -0.75, 1.0e-4), "gain computer: knee midpoint");

        const auto below = BandDynamics::gainOffsetDb (-33.0001f, -30.0f, -12.0f);
        const auto above = BandDynamics::gainOffsetDb (-26.9999f, -30.0f, -12.0f);
        check (near (below, 0.0, 1.0e-3) && near (above, -3.0, 1.0e-3), "gain computer: knee edges are continuous");
    }

    void testSteadyState()
    {
        // Loud: the band cuts by its whole range.
        {
            auto eq = makeEngine (dynamicBell());
            const auto r = run (eq, { 1000.0, 1.0f });
            check (near (r.gainDb, -12.0, 0.5), "loud signal cuts by the range, got " + fmt (r.gainDb));
            check (near (r.dynamicGainDb, -12.0, 0.2), "loud signal offset, got " + fmt (r.dynamicGainDb));
        }

        // Quiet: nothing happens.
        {
            auto eq = makeEngine (dynamicBell());
            const auto r = run (eq, { 1000.0, dbToGain (-50.0f) });
            check (near (r.gainDb, 0.0, 0.05), "quiet signal passes, got " + fmt (r.gainDb));
        }

        // 6 dB over the threshold: about 6 dB of cut.
        {
            auto eq = makeEngine (dynamicBell());
            const auto r = run (eq, { 1000.0, dbToGain (-24.0f) });
            check (near (r.gainDb, -6.0, 0.6), "6 dB over cuts about 6 dB, got " + fmt (r.gainDb));
        }

        // A positive range lifts, on top of the static gain.
        {
            auto s = dynamicBell();
            s.gainDb = -3.0f;
            s.dynamics.rangeDb = 6.0f;
            auto eq = makeEngine (s);
            const auto r = run (eq, { 1000.0, 1.0f });
            check (near (r.gainDb, 3.0, 0.5), "positive range adds to static gain, got " + fmt (r.gainDb));
        }

        // Dynamics off: static gain only.
        {
            auto s = dynamicBell();
            s.dynamics.enabled = false;
            s.gainDb = 4.0f;
            auto eq = makeEngine (s);
            const auto r = run (eq, { 1000.0, 1.0f });
            check (near (r.gainDb, 4.0, 0.05), "dynamics off is a static band, got " + fmt (r.gainDb));
        }
    }

    void testShelves()
    {
        // A dynamic low shelf hears lows (and cuts them) but ignores highs.
        auto s = dynamicBell (200.0f);
        s.type = FilterType::lowShelf;
        s.q = 0.7f;
        s.dynamics.rangeDb = -9.0f;

        {
            auto eq = makeEngine (s);
            const auto r = run (eq, { 50.0, 1.0f });
            check (near (r.gainDb, -9.0, 0.7), "dynamic low shelf cuts loud lows, got " + fmt (r.gainDb));
        }
        {
            auto eq = makeEngine (s);
            const auto r = run (eq, { 8000.0, 1.0f });
            check (r.dynamicGainDb > -0.5f, "dynamic low shelf ignores highs, offset " + fmt (r.dynamicGainDb));
        }

        // And a high shelf the other way round.
        s.type = FilterType::highShelf;
        s.frequency = 4000.0f;
        {
            auto eq = makeEngine (s);
            const auto r = run (eq, { 12000.0, 1.0f });
            check (near (r.dynamicGainDb, -9.0, 0.3), "dynamic high shelf reacts to highs, offset " + fmt (r.dynamicGainDb));
        }
        {
            auto eq = makeEngine (s);
            const auto r = run (eq, { 100.0, 1.0f });
            check (r.dynamicGainDb > -0.5f, "dynamic high shelf ignores lows, offset " + fmt (r.dynamicGainDb));
        }
    }

    void testDetectorFilter()
    {
        // A loud tone well away from a narrow band: the band-filtered
        // detector ignores it, the wide detector reacts.
        auto s = dynamicBell (4000.0f);
        s.q = 4.0f;

        {
            auto eq = makeEngine (s);
            const auto r = run (eq, { 200.0, dbToGain (-12.0f) });
            check (r.dynamicGainDb > -0.5f, "band detector ignores off-band tone, offset " + fmt (r.dynamicGainDb));
        }

        s.dynamics.filter = DetectorFilter::wide;
        {
            auto eq = makeEngine (s);
            const auto r = run (eq, { 200.0, dbToGain (-12.0f) });
            check (near (r.dynamicGainDb, -12.0, 0.3), "wide detector reacts to any tone, offset " + fmt (r.dynamicGainDb));
        }
    }

    void testExternalSidechain()
    {
        auto s = dynamicBell();
        s.dynamics.source = DetectorSource::external;

        // Quiet main signal, loud sidechain: the sidechain drives the cut.
        {
            auto eq = makeEngine (s);
            const Signal key { 1000.0, 1.0f };
            const auto r = run (eq, { 1000.0, dbToGain (-50.0f) }, &key);
            check (near (r.gainDb, -12.0, 0.5), "external sidechain drives the band, got " + fmt (r.gainDb));
        }

        // Loud main signal, silent sidechain: nothing happens.
        {
            auto eq = makeEngine (s);
            const Signal key { 1000.0, 0.0f };
            const auto r = run (eq, { 1000.0, 1.0f }, &key);
            check (near (r.gainDb, 0.0, 0.05), "external band ignores its own input, got " + fmt (r.gainDb));
        }

        // No sidechain connected at all: also nothing.
        {
            auto eq = makeEngine (s);
            const auto r = run (eq, { 1000.0, 1.0f });
            check (near (r.gainDb, 0.0, 0.05), "external band with no sidechain is static, got " + fmt (r.gainDb));
        }

        // The sidechain is filtered too: an off-band key does nothing.
        {
            s.q = 4.0f;
            auto eq = makeEngine (s);
            const Signal key { 100.0, dbToGain (-12.0f) };
            const auto r = run (eq, { 1000.0, dbToGain (-50.0f) }, &key);
            check (r.dynamicGainDb > -0.5f, "sidechain is band filtered, offset " + fmt (r.dynamicGainDb));
        }
    }

    // Feeds a loud 1 kHz tone (or silence) for numSamples and returns the
    // band's dynamic offset afterwards.
    float feed (EqEngine& eq, int numSamples, float amplitude, double& phase)
    {
        juce::AudioBuffer<float> buffer (2, 64);

        for (int done = 0; done < numSamples; done += 64)
        {
            for (int i = 0; i < 64; ++i)
            {
                const auto x = amplitude * (float) std::sin (phase);
                phase = std::fmod (phase + twoPi * 1000.0 / sampleRate, twoPi);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }

            eq.process (buffer);
        }

        return eq.getDynamicGainDb (0);
    }

    void testTiming()
    {
        auto s = dynamicBell();
        s.dynamics.attackMs = 50.0f;
        s.dynamics.releaseMs = 400.0f;

        auto eq = makeEngine (s);
        double phase = 0.0;
        const auto ms = [] (double t) { return (int) (t * 0.001 * sampleRate); };

        const auto early = feed (eq, ms (5), 1.0f, phase);
        check (early > -6.0f, "slow attack has not reached full cut after 5 ms, offset " + fmt (early));

        const auto later = feed (eq, ms (400), 1.0f, phase);
        check (near (later, -12.0, 0.2), "attack settles to the range, offset " + fmt (later));

        const auto released = feed (eq, ms (20), 0.0f, phase);
        check (released < -10.0f, "slow release still holds after 20 ms, offset " + fmt (released));

        const auto gone = feed (eq, ms (4000), 0.0f, phase);
        check (juce::exactlyEqual (gone, 0.0f), "release returns to exactly no offset, offset " + fmt (gone));

        // A fast attack gets most of the way within a few milliseconds.
        s.dynamics.attackMs = 0.5f;
        auto fast = makeEngine (s);
        double fastPhase = 0.0;
        const auto quick = feed (fast, ms (10), 1.0f, fastPhase);
        check (quick < -10.0f, "fast attack cuts within 10 ms, offset " + fmt (quick));
    }

    void testSwitchingOff()
    {
        // Turning dynamics off mid-cut releases the gain rather than jumping.
        auto s = dynamicBell();
        auto eq = makeEngine (s);
        double phase = 0.0;

        feed (eq, (int) (0.3 * sampleRate), 1.0f, phase);

        s.dynamics.enabled = false;
        eq.setBand (0, s);

        const auto justAfter = feed (eq, 64, 1.0f, phase);
        check (justAfter < -10.0f, "switching off releases instead of jumping, offset " + fmt (justAfter));

        const auto settled = feed (eq, (int) (2.0 * sampleRate), 1.0f, phase);
        check (juce::exactlyEqual (settled, 0.0f), "switched-off band returns to its static gain, offset " + fmt (settled));

        // Types without gain never move.
        s = dynamicBell();
        s.type = FilterType::notch;
        auto notch = makeEngine (s);
        double notchPhase = 0.0;
        check (juce::exactlyEqual (feed (notch, (int) sampleRate, 1.0f, notchPhase), 0.0f), "notch bands ignore dynamics");
    }

    void testMidSide()
    {
        // A dynamic band on the side only reacts to side content.
        auto s = dynamicBell();
        s.placement = Placement::side;

        auto eq = makeEngine (s);
        const auto mono = run (eq, { 1000.0, 1.0f }); // L == R: no side signal
        check (juce::exactlyEqual (mono.dynamicGainDb, 0.0f), "side band ignores a mono (all mid) signal, offset " + fmt (mono.dynamicGainDb));
    }

    void testAutomationStability()
    {
        // Random jumps of every dynamic parameter on noise must stay finite
        // and bounded.
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> uni (0.0f, 1.0f);

        EqEngine eq;
        for (int b = 0; b < 4; ++b)
            eq.setBand (b, dynamicBell (200.0f * (float) (b + 1)));
        eq.prepare (sampleRate);

        juce::AudioBuffer<float> buffer (2, 256), side (2, 256);
        auto ok = true;
        float peak = 0.0f;

        for (int block = 0; block < 2000 && ok; ++block)
        {
            for (int b = 0; b < 4; ++b)
            {
                auto s = dynamicBell (20.0f * std::pow (1000.0f, uni (rng)));
                s.type = (FilterType) std::array<int, 4> { 0, 1, 3, 7 }[(size_t) (rng() % 4)];
                s.q = 0.1f * std::pow (100.0f, uni (rng));
                s.gainDb = -20.0f + 40.0f * uni (rng);
                s.dynamics.thresholdDb = -80.0f * uni (rng);
                s.dynamics.rangeDb = -30.0f + 60.0f * uni (rng);
                s.dynamics.attackMs = 0.1f + 100.0f * uni (rng);
                s.dynamics.releaseMs = 5.0f + 500.0f * uni (rng);
                s.dynamics.source = (DetectorSource) (rng() % 2);
                s.dynamics.filter = (DetectorFilter) (rng() % 2);
                s.placement = (Placement) (rng() % numPlacements);
                eq.setBand (b, s);
            }

            for (int i = 0; i < 256; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    buffer.setSample (c, i, uni (rng) * 2.0f - 1.0f);
                    side.setSample (c, i, uni (rng) * 2.0f - 1.0f);
                }

            eq.process (buffer, &side);

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                {
                    const auto v = buffer.getSample (c, i);
                    ok = ok && std::isfinite (v);
                    peak = std::max (peak, std::abs (v));
                }
        }

        check (ok, "dynamic automation stays finite");
        check (peak < 1000.0f, "dynamic automation stays bounded, peak " + fmt (peak));
    }
}

int main()
{
    testGainComputer();
    testSteadyState();
    testShelves();
    testDetectorFilter();
    testExternalSidechain();
    testTiming();
    testSwitchingOff();
    testMidSide();
    testAutomationStability();

    if (failures == 0)
        std::printf ("All dynamic EQ tests passed.\n");

    return failures == 0 ? 0 : 1;
}
