// Offline checks for the smart tools: spectral dynamics (exact rebuild,
// per-bin compression, external detection), curve fitting for EQ Sketch, and
// the spectrum matching behind EQ Match.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "dsp/CurveFit.h"
#include "dsp/EqEngine.h"
#include "dsp/SpectralDynamics.h"
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

    // Amplitude of one frequency in a signal (Goertzel, Hann windowed).
    double amplitudeAt (const std::vector<float>& x, size_t start, size_t length, double hz)
    {
        const auto w = twoPi * hz / sampleRate;
        double re = 0.0, im = 0.0, windowSum = 0.0;

        for (size_t i = 0; i < length; ++i)
        {
            const auto win = 0.5 - 0.5 * std::cos (twoPi * (double) i / (double) length);
            const auto v = (double) x[start + i] * win;
            re += v * std::cos (w * (double) i);
            im -= v * std::sin (w * (double) i);
            windowSum += win;
        }

        return 2.0 * std::sqrt (re * re + im * im) / windowSum;
    }

    double db (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-12)); }

    BandSettings spectralBell (float frequency, float q)
    {
        BandSettings s;
        s.enabled = true;
        s.type = FilterType::bell;
        s.frequency = frequency;
        s.q = q;
        s.dynamics.enabled = true;
        s.dynamics.spectral = true;
        s.dynamics.thresholdDb = -30.0f;
        s.dynamics.rangeDb = -12.0f;
        s.dynamics.attackMs = 5.0f;
        s.dynamics.releaseMs = 50.0f;
        return s;
    }

    struct Tone { double hz, amplitude; };

    // Runs a stereo mix of tones (and optionally sidechain tones) through
    // the spectral stage and returns channel 0 of the output.
    std::vector<float> runSpectral (SpectralDynamics& sd, std::initializer_list<Tone> tones, int numSamples,
                                    std::initializer_list<Tone> sidechainTones = {}, bool withSidechain = false)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, blockSize), sidechain (2, blockSize);
        int n = 0;

        while (n < numSamples)
        {
            for (int i = 0; i < blockSize; ++i, ++n)
            {
                double x = 0.0, s = 0.0;
                for (auto t : tones) x += t.amplitude * std::sin (twoPi * t.hz * n / sampleRate);
                for (auto t : sidechainTones) s += t.amplitude * std::sin (twoPi * t.hz * n / sampleRate);

                buffer.setSample (0, i, (float) x);
                buffer.setSample (1, i, (float) x);
                sidechain.setSample (0, i, (float) s);
                sidechain.setSample (1, i, (float) s);
            }

            sd.process (buffer, withSidechain ? &sidechain : nullptr);

            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }

        return out;
    }

    void testSpectralRebuildsInput()
    {
        // With no band doing anything, the stage is a pure delay.
        SpectralDynamics sd;
        sd.prepare (sampleRate);

        std::mt19937 rng (7);
        std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

        std::vector<float> in, out;
        juce::AudioBuffer<float> buffer (2, blockSize);

        for (int block = 0; block < 40; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = noise (rng);
                in.push_back (x);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, -x);
            }

            sd.process (buffer);

            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }

        auto worst = 0.0;
        for (size_t i = SpectralDynamics::latencySamples; i < out.size(); ++i)
            worst = std::max (worst, (double) std::abs (out[i] - in[i - SpectralDynamics::latencySamples]));

        check (worst < 1.0e-4, "spectral stage rebuilds its input after its latency (worst error " + std::to_string (worst) + ")");
    }

    void testSpectralCutsOnlyLoudBins()
    {
        // A loud tone and a quiet one, both inside one wide band: only the
        // loud one crosses the threshold, so only it is pulled down.
        SpectralDynamics sd;
        sd.prepare (sampleRate);
        sd.setBand (0, spectralBell (1150.0f, 0.7f));
        check (sd.isActive(), "a spectral band makes the stage active");

        const auto out = runSpectral (sd, { { 1000.0, 0.5 }, { 1400.0, 0.01 } }, 48000);
        const auto start = out.size() - 16384;

        // Inputs: 0.5 (-6 dB, 24 dB over the threshold) and 0.01 (-40 dB).
        const auto loud = db (amplitudeAt (out, start, 16384, 1000.0) / 0.5);
        const auto quiet = db (amplitudeAt (out, start, 16384, 1400.0) / 0.01);

        check (loud < -10.0 && loud > -13.0, "loud bin cut by about the range (" + fmt (loud) + " dB)");
        check (std::abs (quiet) < 1.0, "quiet bin next to it untouched (" + fmt (quiet) + " dB)");
        check (sd.getGainDb (0) < -10.0f, "reported gain follows the strongest bin (" + fmt (sd.getGainDb (0)) + " dB)");
    }

    void testSpectralLeavesQuietSignalAlone()
    {
        SpectralDynamics sd;
        sd.prepare (sampleRate);
        sd.setBand (0, spectralBell (1000.0f, 1.0f));

        const auto out = runSpectral (sd, { { 1000.0, 0.01 } }, 32768);
        const auto change = db (amplitudeAt (out, out.size() - 16384, 16384, 1000.0) / 0.01);
        check (std::abs (change) < 0.5, "below the threshold nothing moves (" + fmt (change) + " dB)");
    }

    void testSpectralOutsideBandUntouched()
    {
        SpectralDynamics sd;
        sd.prepare (sampleRate);
        sd.setBand (0, spectralBell (1000.0f, 2.0f));

        // A loud tone far from the band is not this band's business.
        const auto out = runSpectral (sd, { { 8000.0, 0.5 } }, 32768);
        const auto change = db (amplitudeAt (out, out.size() - 16384, 16384, 8000.0) / 0.5);
        check (std::abs (change) < 0.5, "loud tone outside the band untouched (" + fmt (change) + " dB)");
    }

    void testSpectralExternalSidechain()
    {
        auto band = spectralBell (1000.0f, 1.0f);
        band.dynamics.source = DetectorSource::external;

        {
            SpectralDynamics sd;
            sd.prepare (sampleRate);
            sd.setBand (0, band);

            // Loud main signal, silent sidechain: nothing happens.
            const auto out = runSpectral (sd, { { 1000.0, 0.5 } }, 32768, {}, true);
            const auto change = db (amplitudeAt (out, out.size() - 16384, 16384, 1000.0) / 0.5);
            check (std::abs (change) < 0.5, "external: silent sidechain leaves the main signal alone (" + fmt (change) + " dB)");
        }

        {
            SpectralDynamics sd;
            sd.prepare (sampleRate);
            sd.setBand (0, band);

            // Quiet main signal, loud sidechain at the same frequency: ducked.
            const auto out = runSpectral (sd, { { 1000.0, 0.01 } }, 32768, { { 1000.0, 0.5 } }, true);
            const auto change = db (amplitudeAt (out, out.size() - 16384, 16384, 1000.0) / 0.01);
            check (change < -10.0, "external: a loud sidechain ducks the main signal (" + fmt (change) + " dB)");
        }
    }

    void testEqBandStaysStaticWhenSpectral()
    {
        // The regular EQ leaves spectral bands' dynamics to the spectral stage.
        EqEngine eq;
        auto band = spectralBell (1000.0f, 1.0f);
        eq.setBand (0, band);
        eq.prepare (sampleRate);

        juce::AudioBuffer<float> buffer (2, blockSize);
        int n = 0;
        for (int block = 0; block < 40; ++block)
        {
            for (int i = 0; i < blockSize; ++i, ++n)
                for (int c = 0; c < 2; ++c)
                    buffer.setSample (c, i, 0.5f * (float) std::sin (twoPi * 1000.0 * n / sampleRate));

            eq.setBand (0, band);
            eq.process (buffer);
        }

        check (std::abs (eq.getDynamicGainDb (0)) < 0.01f, "EqEngine does not move a spectral band's gain");
        check (std::abs (buffer.getSample (0, blockSize - 1)) > 0.0f, "EqEngine still passes the signal");
    }

    //==========================================================================
    double rmsError (const std::vector<double>& hz, const std::vector<double>& target, const std::vector<BandSettings>& bands)
    {
        const auto total = curvefit::totalCurve (bands, hz, sampleRate);
        auto sum = 0.0;
        for (size_t i = 0; i < hz.size(); ++i)
            sum += (target[i] - total[i]) * (target[i] - total[i]);
        return std::sqrt (sum / (double) hz.size());
    }

    void testFitFlat()
    {
        const auto hz = curvefit::logFrequencies();
        const std::vector<double> flat (hz.size(), 0.0);
        check (curvefit::fit (hz, flat, sampleRate).empty(), "a flat curve needs no bands");
    }

    void testFitSingleBell()
    {
        const auto hz = curvefit::logFrequencies();

        BandSettings bell;
        bell.enabled = true;
        bell.frequency = 2500.0f;
        bell.gainDb = 6.0f;
        bell.q = 2.0f;

        const auto target = curvefit::totalCurve (std::vector<BandSettings> { bell }, hz, sampleRate);
        const auto bands = curvefit::fit (hz, target, sampleRate);

        check (! bands.empty() && bands.size() <= 2, "one bell is fitted with one or two bands (got " + std::to_string (bands.size()) + ")");

        if (! bands.empty())
        {
            const auto& b = bands.front();
            check (b.type == FilterType::bell, "fitted band is a bell");
            check (std::abs (std::log2 (b.frequency / 2500.0f)) < 0.1, "fitted frequency near 2.5 kHz (" + fmt (b.frequency) + ")");
            check (std::abs (b.gainDb - 6.0f) < 0.5f, "fitted gain near +6 dB (" + fmt (b.gainDb) + ")");
        }

        const auto err = rmsError (hz, target, bands);
        check (err < 0.15, "single bell fit error small (" + fmt (err) + " dB rms)");
    }

    void testFitComplexCurve()
    {
        // A curve built from several bands, including a shelf, is redrawn closely.
        const auto hz = curvefit::logFrequencies();

        std::vector<BandSettings> source (4);
        for (auto& s : source) s.enabled = true;
        source[0].type = FilterType::lowShelf;  source[0].frequency = 120.0f;  source[0].gainDb = 4.0f;  source[0].q = 0.7f;
        source[1].frequency = 400.0f;  source[1].gainDb = -5.0f; source[1].q = 1.5f;
        source[2].frequency = 3000.0f; source[2].gainDb = 3.0f;  source[2].q = 0.8f;
        source[3].type = FilterType::highShelf; source[3].frequency = 9000.0f; source[3].gainDb = -6.0f; source[3].q = 0.7f;

        const auto target = curvefit::totalCurve (source, hz, sampleRate);

        curvefit::Options o;
        o.maxBands = 8;
        const auto bands = curvefit::fit (hz, target, sampleRate, o);
        const auto err = rmsError (hz, target, bands);

        check (bands.size() <= 8, "fit respects the band limit");
        check (err < 0.4, "four-band curve fitted closely (" + fmt (err) + " dB rms, " + std::to_string (bands.size()) + " bands)");
    }

    void testFitSketchShape()
    {
        // A hand-drawn style shape: a raised plateau with hard edges. It
        // cannot be matched exactly, but the fit should get most of it.
        const auto hz = curvefit::logFrequencies();
        std::vector<double> target (hz.size(), 0.0);
        for (size_t i = 0; i < hz.size(); ++i)
            if (hz[i] > 500.0 && hz[i] < 2000.0)
                target[i] = 5.0;

        auto before = 0.0;
        for (auto t : target) before += t * t;
        before = std::sqrt (before / (double) target.size());

        const auto bands = curvefit::fit (hz, target, sampleRate);
        const auto err = rmsError (hz, target, bands);
        check (err < 0.5 * before, "plateau fit halves the error at least (" + fmt (before) + " -> " + fmt (err) + " dB rms)");
    }

    //==========================================================================
    void testMatchLearnsDifference()
    {
        // Reference = the source through a +6 dB bell at 2 kHz and 6 dB
        // quieter overall. The match curve finds the bell but not the level.
        std::mt19937 rng (3);
        std::normal_distribution<float> noise (0.0f, 0.1f);

        EqEngine eq;
        BandSettings bell;
        bell.enabled = true;
        bell.frequency = 2000.0f;
        bell.gainDb = 6.0f;
        bell.q = 1.0f;
        eq.setBand (0, bell);
        eq.prepare (sampleRate);

        LongTermSpectrum source, reference;
        juce::AudioBuffer<float> buffer (1, blockSize);
        std::vector<float> dry ((size_t) blockSize);

        for (int block = 0; block < 400; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                dry[(size_t) i] = noise (rng);
                buffer.setSample (0, i, dry[(size_t) i]);
            }

            eq.process (buffer);
            buffer.applyGain (0.5f);

            source.push (dry.data(), blockSize);
            reference.push (buffer.getReadPointer (0), blockSize);
        }

        check (source.getSeconds (sampleRate) > 4.0, "learned several seconds (" + fmt (source.getSeconds (sampleRate)) + " s)");

        const auto hz = curvefit::logFrequencies();
        const auto curve = match::curve (hz, source.levelsDb (hz, sampleRate), reference.levelsDb (hz, sampleRate));

        auto at = [&] (double f)
        {
            size_t best = 0;
            for (size_t i = 0; i < hz.size(); ++i)
                if (std::abs (std::log (hz[i] / f)) < std::abs (std::log (hz[best] / f)))
                    best = i;
            return curve[best];
        };

        // The mean offset is removed, so the bell's average lift shifts the
        // whole curve down a little: compare the bell to its surroundings.
        const auto lift = at (2000.0) - at (150.0);
        check (std::abs (lift - 6.0) < 1.0, "match curve finds the +6 dB bell (" + fmt (lift) + " dB)");
        check (std::abs (at (150.0) - at (60.0)) < 1.0, "match curve flat away from the bell");

        // And the fitted bands reproduce it.
        const auto bands = curvefit::fit (hz, curve, sampleRate);
        check (! bands.empty(), "match curve turns into bands");
        check (rmsError (hz, curve, bands) < 0.6, "fitted bands follow the match curve (" + fmt (rmsError (hz, curve, bands)) + " dB rms)");
    }

    void testMatchIgnoresSilence()
    {
        const auto hz = curvefit::logFrequencies();
        std::vector<double> src (hz.size(), -40.0), ref (hz.size(), -40.0);
        ref[10] = -200.0; // silent in the reference at one point

        const auto curve = match::curve (hz, src, ref);
        check (std::abs (curve[10]) < 1.0e-9, "silent frequencies are left alone");

        const auto half = match::curve (hz, src, std::vector<double> (hz.size(), -30.0), 0.5);
        check (std::abs (half[50]) < 1.0e-9, "a pure level difference is not matched");
    }
}

int main()
{
    testSpectralRebuildsInput();
    testSpectralCutsOnlyLoudBins();
    testSpectralLeavesQuietSignalAlone();
    testSpectralOutsideBandUntouched();
    testSpectralExternalSidechain();
    testEqBandStaysStaticWhenSpectral();

    testFitFlat();
    testFitSingleBell();
    testFitComplexCurve();
    testFitSketchShape();

    testMatchLearnsDifference();
    testMatchIgnoresSilence();

    if (failures == 0)
        std::printf ("All smart tools tests passed.\n");

    return failures == 0 ? 0 : 1;
}
