// Offline checks for the character stage: Clean must be untouched, Gentle and
// Warm must keep quiet signals at unity gain, add the expected harmonics at
// higher levels, keep aliasing far below a non-oversampled shaper, add no DC,
// and switch modes without clicks.

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "dsp/Character.h"

using namespace fabcutie::dsp;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr double twoPi = 2.0 * juce::MathConstants<double>::pi;

    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        std::printf ("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (! ok)
            ++failures;
    }

    std::string fmt (double v)
    {
        char text[32];
        std::snprintf (text, sizeof (text), "%.2f", v);
        return text;
    }

    CharacterStage makeStage (CharacterMode mode)
    {
        CharacterStage stage;
        stage.setMode (mode);
        stage.prepare (sampleRate, blockSize, 2);
        return stage;
    }

    // Runs a stereo sine through the stage and returns the left output after
    // a one-second settle.
    std::vector<float> runSine (CharacterStage& stage, double freq, double amplitude, int length)
    {
        const auto settle = (int) sampleRate;
        juce::AudioBuffer<float> buffer (2, blockSize);
        std::vector<float> out;
        out.reserve ((size_t) length);
        double phase = 0.0;

        for (int done = 0; done < settle + length; done += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = (float) (amplitude * std::sin (phase));
                phase = std::fmod (phase + twoPi * freq / sampleRate, twoPi);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }

            stage.process (buffer);

            for (int i = 0; i < blockSize; ++i)
                if (done + i >= settle && (int) out.size() < length)
                    out.push_back (buffer.getSample (0, i));
        }

        return out;
    }

    // Amplitude of one frequency in a signal, Hann-windowed so leakage from
    // the (much louder) fundamental doesn't mask small components.
    double amplitudeAt (const std::vector<float>& x, double freq)
    {
        double re = 0.0, im = 0.0, windowSum = 0.0;
        const auto n = x.size();

        for (size_t i = 0; i < n; ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (twoPi * (double) i / (double) (n - 1));
            const auto p = twoPi * freq * (double) i / sampleRate;
            re += w * x[i] * std::cos (p);
            im -= w * x[i] * std::sin (p);
            windowSum += w;
        }

        return 2.0 * std::sqrt (re * re + im * im) / windowSum;
    }

    double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-12)); }

    // Where a harmonic lands at the output rate after folding.
    double folded (double f)
    {
        f = std::fmod (f, sampleRate);
        return f > sampleRate / 2 ? sampleRate - f : f;
    }

    void testCleanIsBitExact()
    {
        auto stage = makeStage (CharacterMode::clean);
        std::mt19937 rng (1);
        std::uniform_real_distribution<float> dist (-2.0f, 2.0f);
        juce::AudioBuffer<float> buffer (2, blockSize), copy (2, blockSize);
        auto identical = true;

        for (int block = 0; block < 50; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, dist (rng));

            copy.makeCopyOf (buffer);
            stage.process (buffer);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    identical = identical && juce::exactlyEqual (buffer.getSample (ch, i), copy.getSample (ch, i));
        }

        check (identical, "Clean passes audio through bit-exact");
    }

    void testShaperCurves()
    {
        // Unity slope at zero, so small signals pass at unity gain.
        const auto h = 1.0e-3f;
        check (std::abs ((shaper::gentle (h) - shaper::gentle (-h)) / (2 * h) - 1.0f) < 1.0e-3f, "Gentle curve has unit slope at 0");
        check (std::abs ((shaper::warm (h) - shaper::warm (-h)) / (2 * h) - 1.0f) < 1.0e-3f, "Warm curve has unit slope at 0");
        check (juce::exactlyEqual (shaper::gentle (0.0f), 0.0f) && std::abs (shaper::warm (0.0f)) < 1.0e-7f, "Both curves pass through 0");

        // Gentle never gets louder than the input. Warm's bias stretches the
        // negative half very slightly, but by less than 0.2 dB.
        auto bounded = true;
        for (float x = -1.0f; x <= 1.0f; x += 0.01f)
            bounded = bounded && std::abs (shaper::gentle (x)) <= std::abs (x) + 1.0e-6f
                              && std::abs (shaper::warm (x))   <= std::abs (x) * 1.023f + 1.0e-6f;
        check (bounded, "Curves stay within 0.2 dB of the input level in [-1, 1]");
    }

    void testSmallSignalUnity()
    {
        for (auto mode : { CharacterMode::gentle, CharacterMode::warm })
            for (auto freq : { 100.0, 1000.0, 10000.0 })
            {
                auto stage = makeStage (mode);
                const auto amplitude = 0.01; // -40 dBFS
                const auto out = runSine (stage, freq, amplitude, 24000);
                const auto gain = db (amplitudeAt (out, freq) / amplitude);
                check (std::abs (gain) < 0.05,
                       std::string (mode == CharacterMode::gentle ? "Gentle" : "Warm") + " is unity at -40 dBFS, "
                           + fmt (freq) + " Hz (" + fmt (gain) + " dB)");
            }
    }

    void testHarmonics()
    {
        const auto f = 1000.0, amplitude = 0.5; // -6 dBFS

        {
            auto stage = makeStage (CharacterMode::gentle);
            const auto out = runSine (stage, f, amplitude, 48000);
            const auto fund = amplitudeAt (out, f);
            const auto h2 = db (amplitudeAt (out, 2 * f) / fund), h3 = db (amplitudeAt (out, 3 * f) / fund);
            check (h3 > -60.0 && h3 < -30.0, "Gentle adds a subtle 3rd harmonic at -6 dBFS (" + fmt (h3) + " dBc)");
            check (h2 < -90.0, "Gentle adds no 2nd harmonic (" + fmt (h2) + " dBc)");
        }

        {
            auto stage = makeStage (CharacterMode::warm);
            const auto out = runSine (stage, f, amplitude, 48000);
            const auto fund = amplitudeAt (out, f);
            const auto h2 = db (amplitudeAt (out, 2 * f) / fund);
            check (h2 > -45.0 && h2 < -20.0, "Warm adds a 2nd harmonic at -6 dBFS (" + fmt (h2) + " dBc)");

            double mean = 0.0;
            for (auto x : out) mean += x;
            mean /= (double) out.size();
            check (std::abs (mean) < 1.0e-4, "Warm adds no DC offset (mean " + std::to_string (mean) + ")");
        }
    }

    // A loud 7 kHz sine makes harmonics at 21, 28, 35 kHz... Above 24 kHz they
    // would fold back to 20, 13, 6 kHz without oversampling.
    void testAliasing()
    {
        const auto f = 7000.0, amplitude = 1.0;

        for (auto mode : { CharacterMode::gentle, CharacterMode::warm })
        {
            auto stage = makeStage (mode);
            const auto out = runSine (stage, f, amplitude, 48000);

            // The same curve applied directly at 48 kHz, for comparison.
            std::vector<float> naive (out.size());
            for (size_t i = 0; i < naive.size(); ++i)
            {
                const auto x = (float) (amplitude * std::sin (twoPi * f * (double) i / sampleRate));
                naive[i] = mode == CharacterMode::gentle ? shaper::gentle (x) : shaper::warm (x);
            }

            auto worst = -300.0, worstNaive = -300.0;
            for (int k = 4; k <= 9; ++k)
            {
                const auto alias = folded (k * f);
                worst      = std::max (worst,      db (amplitudeAt (out, alias)   / amplitudeAt (out, f)));
                worstNaive = std::max (worstNaive, db (amplitudeAt (naive, alias) / amplitudeAt (naive, f)));
            }

            const auto name = std::string (mode == CharacterMode::gentle ? "Gentle" : "Warm");
            check (worst < -70.0, name + " aliasing at 0 dBFS stays below -70 dBc (" + fmt (worst)
                                     + " dBc, " + fmt (worstNaive) + " dBc without oversampling)");
        }
    }

    void testModeSwitchIsSmooth()
    {
        CharacterStage stage;
        stage.prepare (sampleRate, blockSize, 2);

        const auto f = 200.0, amplitude = 0.9;
        const auto maxStep = twoPi * f / sampleRate * amplitude; // steepest step of the sine itself
        juce::AudioBuffer<float> buffer (2, blockSize);
        double phase = 0.0;
        float last = 0.0f, worstStep = 0.0f;
        auto finite = true;
        const CharacterMode sequence[] { CharacterMode::gentle, CharacterMode::warm, CharacterMode::clean,
                                         CharacterMode::warm, CharacterMode::gentle, CharacterMode::clean };

        for (int block = 0; block < 600; ++block)
        {
            if (block % 100 == 50)
                stage.setMode (sequence[block / 100]);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = (float) (amplitude * std::sin (phase));
                phase = std::fmod (phase + twoPi * f / sampleRate, twoPi);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }

            stage.process (buffer);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto y = buffer.getSample (0, i);
                finite = finite && std::isfinite (y);
                if (block > 0 || i > 0) worstStep = std::max (worstStep, std::abs (y - last));
                last = y;
            }
        }

        check (finite, "Mode switching stays finite");
        check (worstStep < 1.3f * (float) maxStep, "Mode switching has no clicks (largest step "
                                                       + fmt (worstStep / maxStep) + "x the sine's own)");
    }

    void testLoudAndOversizedBlocks()
    {
        auto stage = makeStage (CharacterMode::warm);
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> dist (-8.0f, 8.0f); // +18 dBFS
        juce::AudioBuffer<float> buffer (2, blockSize * 3 + 17);  // more than prepared
        auto finite = true;
        auto bounded = true;

        for (int block = 0; block < 40; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, dist (rng));

            stage.process (buffer);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    finite = finite && std::isfinite (buffer.getSample (ch, i));
                    bounded = bounded && std::abs (buffer.getSample (ch, i)) < 20.0f;
                }
        }

        check (finite && bounded, "Hot input and blocks larger than prepared stay finite");
    }
}

int main()
{
    std::printf ("Oversampling latency: %.2f samples\n", (double) makeStage (CharacterMode::warm).getOversamplingLatency());

    testCleanIsBitExact();
    testShaperCurves();
    testSmallSignalUnity();
    testHarmonics();
    testAliasing();
    testModeSwitchIsSmooth();
    testLoudAndOversizedBlocks();

    std::printf (failures == 0 ? "All character tests passed\n" : "%d character test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
