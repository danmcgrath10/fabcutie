// Offline checks for the EQ engine: measured sine responses must match the
// analytic curve, slopes and placements must behave, and the filters must
// stay finite under heavy parameter automation. Also covers the analyzer
// plumbing: the audio taps, the spectrum analyzer, band solo and the meter.

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "dsp/AudioTap.h"
#include "dsp/BandSolo.h"
#include "dsp/ChannelLayout.h"
#include "dsp/EqEngine.h"
#include "dsp/Notes.h"
#include "dsp/PeakMeter.h"
#include "dsp/SpectrumAnalyzer.h"
#include "ui/GraphGeometry.h"

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
            band (FilterType::allPass, 1000, 0, 2),
            band (FilterType::flatTilt, 1000, 12, 1),
            band (FilterType::flatTilt, 300, -30, 1),
            band (FilterType::lowCut, 200, 0, 1, brickwallSlopeIndex),
            band (FilterType::highCut, 3000, 0, 1, brickwallSlopeIndex),
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

    void testFilterExtras()
    {
        auto db = [] (const BandSettings& s, double f) { return bandMagnitudeDb (s, f, sampleRate); };

        // All pass: flat magnitude, -180 degrees of phase at its frequency.
        for (auto q : { 0.1f, 1.0f, 10.0f })
        {
            const auto ap = band (FilterType::allPass, 1000, 0, q);
            auto flat = true;
            for (double f = 10; f < 24000; f *= 1.1)
                flat = flat && std::abs (db (ap, f)) < 1.0e-9;
            check (flat, "all pass is flat at Q " + std::to_string (q));

            const auto h = designResponse (designBand (ap, sampleRate), 1000, sampleRate);
            check (std::abs (std::abs (std::arg (h)) - 3.14159265358979) < 1.0e-6, "all pass turns the phase 180 degrees at its frequency");
        }

        // Flat tilt: a straight line through the pivot, gain / 10 dB per octave.
        for (auto gain : { -30.0f, -12.0f, 3.0f, 12.0f, 30.0f })
            for (auto pivot : { 100.0f, 1000.0f, 8000.0f })
            {
                const auto ft = band (FilterType::flatTilt, pivot, gain, 1);
                const auto slope = gain / flatTiltOctaves;
                auto worst = 0.0;

                for (double f = 20; f <= 16000; f *= 1.05)
                    worst = std::max (worst, std::abs (db (ft, f) - slope * std::log2 (f / pivot)));

                check (std::abs (db (ft, pivot)) < 1.0e-6, "flat tilt is 0 dB at its pivot");
                check (worst < 0.1, "flat tilt " + std::to_string (gain) + " dB at " + std::to_string (pivot)
                                         + " Hz strays " + std::to_string (worst) + " dB from a straight line");
            }

        check (std::abs (db (band (FilterType::flatTilt, 1000, 12, 1), 20000) - db (band (FilterType::flatTilt, 1000, 12, 1), 20) - 12.0) < 0.1,
               "flat tilt gain is the change across 20 Hz to 20 kHz");
        check (std::abs (db (band (FilterType::flatTilt, 500, 0, 1), 5000)) < 1.0e-9, "flat tilt at 0 dB is flat");

        // Brickwall: flat pass band, -3 dB at the cutoff, 100 dB down within half an octave.
        for (auto fc : { 100.0, 1000.0, 10000.0 })
        {
            const auto hc = band (FilterType::highCut, (float) fc, 0, 1, brickwallSlopeIndex);
            const auto lc = band (FilterType::lowCut, (float) fc, 0, 1, brickwallSlopeIndex);
            const auto name = "brickwall at " + std::to_string (fc) + " Hz";

            check (std::abs (db (hc, fc) + 3.01) < 0.05 && std::abs (db (lc, fc) + 3.01) < 0.05, name + " is -3 dB at the cutoff");
            check (db (hc, fc * 1.4) < -99.0 && db (lc, fc / 1.4) < -99.0, name + " is 100 dB down half an octave out");
            check (std::abs (db (hc, fc / 10)) < 0.01 && std::abs (db (lc, std::min (fc * 10, 20000.0))) < 0.01,
                   name + " is flat in the pass band");

            auto noBoost = true;
            for (double f = 10; f < 24000; f *= 1.02)
                noBoost = noBoost && db (hc, f) < 1.0e-6 && db (lc, f) < 1.0e-6;
            check (noBoost, name + " never rises above 0 dB");
        }

        // Out-of-range slope values clamp to the brickwall instead of misbehaving.
        check (std::abs (db (band (FilterType::highCut, 1000, 0, 1, 99), 1000) + 3.01) < 0.05, "slope index clamps");
    }

    void testNotes()
    {
        check (std::abs (frequencyForNote (69) - 440.0) < 1.0e-9, "A4 is 440 Hz");
        check (std::abs (noteForFrequency (261.6255653) - 60.0) < 1.0e-6, "middle C is note 60");
        check (std::abs (snapToNote (452.0) - 440.0) < 1.0e-9, "452 Hz snaps to A4");
        check (std::abs (snapToNote (455.0) - 466.1637615) < 1.0e-6, "455 Hz snaps to A#4");
        check (std::abs (snapToNote (30.0) - 30.86770633) < 1.0e-6, "30 Hz snaps to B0");
        check (noteName (60) == "C4" && noteName (61) == "C#4" && noteName (21) == "A0" && noteName (0) == "C-1", "note names");
        check (isBlackKey (61) && ! isBlackKey (60) && ! isBlackKey (64) && isBlackKey (70), "black keys");
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
                s.slopeIndex = (int) (uni (rng) * (numSlopes - 0.001f));
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

    // Surround: placements pick speakers by side, every channel keeps its own
    // filter memory, and the largest layout (9.1.6) is handled in full.
    std::vector<double> measureSurround (const juce::AudioChannelSet& layout, const BandSettings& b, double freq)
    {
        EqEngine eq;
        eq.setChannelMap (ChannelMap::fromLayout (layout));
        eq.setBand (0, b);
        eq.prepare (sampleRate);

        const auto numChannels = layout.size();
        juce::AudioBuffer<float> buffer (numChannels, blockSize);
        std::vector<double> out ((size_t) numChannels, 0.0);
        double phase = 0.0, in = 0.0;
        const auto inc = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;

        for (int block = 0; block < 150; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = (float) std::sin (phase);
                phase = std::fmod (phase + inc, 2.0 * juce::MathConstants<double>::pi);
                for (int c = 0; c < numChannels; ++c)
                    buffer.setSample (c, i, x);
                if (block >= 100) in += (double) x * x;
            }

            eq.process (buffer);

            if (block >= 100)
                for (int c = 0; c < numChannels; ++c)
                    for (int i = 0; i < blockSize; ++i)
                        out[(size_t) c] += juce::square ((double) buffer.getSample (c, i));
        }

        for (auto& o : out)
            o = 10.0 * std::log10 (std::max (o, 1.0e-30) / in);

        return out;
    }

    void testSurround()
    {
        using CT = juce::AudioChannelSet::ChannelType;
        const auto layout = juce::AudioChannelSet::create5point1();
        const auto map = ChannelMap::fromLayout (layout);

        check (map.isSurround() && map.numChannels == 6, "surround: 5.1 has six channels");
        check (ChannelMap::sideOf (CT::leftSurround) == ChannelSide::left && ChannelMap::sideOf (CT::rightSurround) == ChannelSide::right
                   && ChannelMap::sideOf (CT::centre) == ChannelSide::centre && ChannelMap::sideOf (CT::LFE) == ChannelSide::centre,
               "surround: speaker sides");

        const auto expect = [&] (Placement p, const std::string& name, auto shouldBoost)
        {
            const auto db = measureSurround (layout, band (FilterType::bell, 1000, 12, 1, 1, p), 1000);
            auto ok = true;

            for (int c = 0; c < layout.size(); ++c)
            {
                const auto boosted = shouldBoost (map.sides[(size_t) c]);
                ok = ok && (boosted ? std::abs (db[(size_t) c] - 12.0) < 0.1 : std::abs (db[(size_t) c]) < 1.0e-6);
            }

            check (ok, "surround: " + name);
        };

        expect (Placement::stereo, "stereo placement boosts every speaker", [] (ChannelSide) { return true; });
        expect (Placement::left,   "left placement boosts only left speakers", [] (ChannelSide s) { return s == ChannelSide::left; });
        expect (Placement::right,  "right placement boosts only right speakers", [] (ChannelSide s) { return s == ChannelSide::right; });
        expect (Placement::mid,    "mid placement boosts only the centre line", [] (ChannelSide s) { return s == ChannelSide::centre; });
        expect (Placement::side,   "side placement boosts every speaker off the centre", [] (ChannelSide s) { return s != ChannelSide::centre; });

        const auto big = juce::AudioChannelSet::create9point1point6();
        check (big.size() == 16 && isSupportedSurroundLayout (big), "surround: 9.1.6 is offered");
        check (! isSupportedSurroundLayout (juce::AudioChannelSet::discreteChannels (4)), "surround: discrete layouts are not offered");
        check (! isSupportedSurroundLayout (juce::AudioChannelSet::ambisonic (1)), "surround: ambisonics are not offered");

        const auto db = measureSurround (big, band (FilterType::highShelf, 2000, -9, 0.7f), 15000);
        auto all = true;
        for (auto d : db)
            all = all && std::abs (d + 9.0) < 0.2;
        check (all, "surround: a 9.1.6 band processes all 16 channels");
    }

    // The graph's pixel mapping must round-trip, so dragging a node lands
    // exactly where the pointer is.
    void testGraphGeometry()
    {
        fabcutie::ui::GraphGeometry geo;
        geo.x = 20.0f; geo.width = 900.0f; geo.y = 14.0f; geo.height = 500.0f; geo.rangeDb = 12.0f;

        check (std::abs (geo.xForFrequency (geo.minHz) - geo.x) < 1.0e-3f, "graph: lowest frequency at the left edge");
        check (std::abs (geo.xForFrequency (geo.maxHz) - (geo.x + geo.width)) < 1.0e-2f, "graph: highest frequency at the right edge");
        check (std::abs (geo.yForDb (0.0f) - (geo.y + geo.height * 0.5f)) < 1.0e-3f, "graph: 0 dB in the middle");
        check (std::abs (geo.yForDb (12.0f) - geo.y) < 1.0e-3f, "graph: +range at the top");

        for (float hz : { 10.0f, 47.0f, 1000.0f, 12345.0f, 30000.0f })
            check (std::abs (geo.frequencyForX (geo.xForFrequency (hz)) / hz - 1.0f) < 1.0e-4f,
                   "graph: frequency round-trip at " + std::to_string (hz));

        for (float db : { -30.0f, -3.5f, 0.0f, 7.25f, 30.0f })
            check (std::abs (geo.dbForY (geo.yForDb (db)) - db) < 1.0e-4f, "graph: gain round-trip at " + std::to_string (db));
    }

    // The tap must deliver the channel average in order, and drop (not
    // overwrite) what does not fit while nobody reads.
    void testAudioTap()
    {
        AudioTap tap;
        std::vector<float> left (1000), right (1000);
        for (int i = 0; i < 1000; ++i)
        {
            left[(size_t) i] = (float) i;
            right[(size_t) i] = (float) -i * 0.5f;
        }

        const float* channels[] { left.data(), right.data() };
        tap.push (channels, 2, 1000);

        std::vector<float> out (2000);
        const auto got = tap.pull (out.data(), (int) out.size());
        check (got == 1000, "tap: delivers every pushed sample");

        bool exact = true;
        for (int i = 0; i < got; ++i)
            exact = exact && std::abs (out[(size_t) i] - (float) i * 0.25f) < 1.0e-4f;
        check (exact, "tap: mono average of the channels, in order");

        for (int block = 0; block < 100; ++block)
            tap.push (channels, 2, 1000);
        check (tap.getNumReady() < AudioTap::capacity, "tap: a full tap drops new samples");
        check (tap.pull (out.data(), 1) == 1 && std::abs (out[0]) < 1.0e-6f, "tap: oldest samples kept when full");
    }

    std::vector<float> sine (double freq, float amplitude, int length)
    {
        std::vector<float> x ((size_t) length);
        for (int i = 0; i < length; ++i)
            x[(size_t) i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sampleRate);
        return x;
    }

    float analyse (SpectrumAnalyzer& analyzer, const std::vector<float>& signal, float atHz)
    {
        analyzer.setSampleRate (sampleRate);
        analyzer.reset();

        for (size_t start = 0; start + 512 <= signal.size(); start += 512)
        {
            analyzer.push (signal.data() + start, 512);
            analyzer.update (512.0 / sampleRate, 0.05f);
        }

        // Half a bin either side, as the display asks for one pixel's span.
        const auto halfBin = (float) sampleRate / (float) analyzer.getFftSize() * 0.5f;
        return analyzer.levelForSpan (atHz - halfBin, atHz + halfBin);
    }

    // A full-scale sine must read about 0 dB at its frequency (within the
    // Hann window's scalloping loss) and the rest of the spectrum must be low.
    void testSpectrumAnalyzer()
    {
        for (int order = SpectrumAnalyzer::minOrder; order <= SpectrumAnalyzer::maxOrder; ++order)
        {
            SpectrumAnalyzer analyzer (order);
            const auto name = "analyzer " + std::to_string (1 << order) + ": ";
            const auto signal = sine (1000.0, 1.0f, (int) sampleRate);

            const auto peak = analyse (analyzer, signal, 1000.0f);
            check (peak < 0.2f && peak > -1.6f, name + "0 dBFS sine reads 0 dB (got " + std::to_string (peak) + ")");

            const auto far = std::max (analyzer.levelForSpan (90.0f, 110.0f), analyzer.levelForSpan (9000.0f, 11000.0f));
            check (far < -80.0f, name + "little leakage away from the sine (got " + std::to_string (far) + ")");

            const auto quiet = analyse (analyzer, sine (1000.0, 0.01f, (int) sampleRate), 1000.0f);
            check (std::abs (quiet - peak + 40.0f) < 0.5f, name + "level follows amplitude (-40 dB)");
        }

        // The display takes the loudest bin over wide spans, so a narrow peak
        // at high frequencies is not averaged away.
        SpectrumAnalyzer analyzer (13);
        analyse (analyzer, sine (12000.0, 0.5f, (int) sampleRate), 12000.0f);
        check (analyzer.levelForSpan (11000.0f, 13000.0f) > -8.0f, "analyzer: wide spans keep narrow peaks");

        // Smoothing: after the signal stops the level falls, not jumps.
        analyzer.setSampleRate (sampleRate);
        const std::vector<float> silence (1024, 0.0f);
        analyzer.push (silence.data(), 1024);
        analyzer.update (0.02, 1.0f);
        const auto afterSilence = analyzer.levelForSpan (11900.0f, 12100.0f);
        check (afterSilence > -30.0f && afterSilence < -6.0f, "analyzer: slow release falls gradually");
    }

    double soloGainDb (FilterType type, float bandHz, float bandQ, double testHz, bool enabled = true)
    {
        BandSolo solo;
        auto settings = band (type, bandHz, 6.0f, bandQ);
        settings.enabled = enabled;
        solo.setBand (settings);
        solo.prepare (sampleRate);

        juce::AudioBuffer<float> buffer (2, blockSize);
        double phase = 0.0, in = 0.0, out = 0.0;
        const auto inc = 2.0 * juce::MathConstants<double>::pi * testHz / sampleRate;

        for (int block = 0; block < 150; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = (float) std::sin (phase);
                phase = std::fmod (phase + inc, 2.0 * juce::MathConstants<double>::pi);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
                if (block >= 50) in += (double) x * x;
            }

            solo.process (buffer, 2);

            if (block >= 50)
                for (int i = 0; i < blockSize; ++i)
                    out += juce::square ((double) buffer.getSample (0, i));
        }

        return 10.0 * std::log10 (std::max (out, 1.0e-30) / in);
    }

    // Solo must keep the band's own region and drop the rest of the spectrum.
    void testBandSolo()
    {
        check (std::abs (soloGainDb (FilterType::bell, 1000, 2, 1000)) < 0.5, "solo bell: passes its frequency");
        check (soloGainDb (FilterType::bell, 1000, 2, 100) < -15.0, "solo bell: drops low frequencies");
        check (soloGainDb (FilterType::bell, 1000, 2, 10000) < -15.0, "solo bell: drops high frequencies");
        check (std::abs (soloGainDb (FilterType::notch, 3000, 8, 3000)) < 0.5, "solo notch: passes the notched frequency");

        check (std::abs (soloGainDb (FilterType::lowCut, 200, 1, 40)) < 0.5, "solo low cut: passes what it removes");
        check (soloGainDb (FilterType::lowCut, 200, 1, 2000) < -30.0, "solo low cut: drops what it keeps");
        check (std::abs (soloGainDb (FilterType::highShelf, 5000, 1, 15000)) < 0.5, "solo high shelf: passes the shelf");
        check (soloGainDb (FilterType::highShelf, 5000, 1, 500) < -30.0, "solo high shelf: drops below it");

        for (double hz : { 50.0, 1000.0, 15000.0 })
            check (std::abs (soloGainDb (FilterType::bell, 1000, 2, hz, false)) < 0.01, "solo off: signal untouched");
    }

    void testPeakMeter()
    {
        PeakMeter meter;
        juce::AudioBuffer<float> buffer (2, 64);
        buffer.clear();
        buffer.setSample (0, 10, -0.5f);
        buffer.setSample (1, 20, 0.25f);
        meter.process (buffer, 2);

        buffer.clear();
        buffer.setSample (0, 5, 0.1f);
        meter.process (buffer, 2);

        check (std::abs (meter.takePeak (0) - 0.5f) < 1.0e-6f, "meter: keeps the highest peak until read");
        check (std::abs (meter.takePeak (1) - 0.25f) < 1.0e-6f, "meter: channels are separate");
        check (juce::exactlyEqual (meter.takePeak (0), 0.0f), "meter: reading clears the peak");
    }
}

int main()
{
    testPassThrough();
    testShapes();
    testResponseMatchesAnalytic();
    testFilterExtras();
    testNotes();
    testPlacement();
    testMono();
    testSurround();
    testSmoothSwitching();
    testAutomationStability();
    testGraphGeometry();
    testAudioTap();
    testSpectrumAnalyzer();
    testBandSolo();
    testPeakMeter();

    if (failures == 0)
        std::printf ("All tests passed.\n");
    else
        std::printf ("%d EQ engine test(s) failed.\n", failures);

    return failures == 0 ? 0 : 1;
}
