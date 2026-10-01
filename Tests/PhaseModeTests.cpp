// Offline checks for natural and linear phase: latency, how closely the FIR
// kernels follow the analog curves (magnitude and, for natural phase, phase),
// no cramping near Nyquist, mid/side routing, that the streaming convolution
// equals a direct convolution, and that kernel and mode changes don't click.

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "dsp/PhaseModes.h"

using namespace fabcutie::dsp;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double pi = 3.14159265358979323846;
    constexpr int blockSize = 480; // deliberately not a multiple of the partition

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
        std::snprintf (text, sizeof (text), "%.3f", v);
        return text;
    }

    using Bands = std::array<BandSettings, maxBands>;

    BandSettings band (FilterType type, float freq, float gainDb, float q, Placement placement = Placement::stereo, int slope = 1)
    {
        BandSettings s;
        s.enabled = true;
        s.type = type;
        s.frequency = freq;
        s.gainDb = gainDb;
        s.q = q;
        s.placement = placement;
        s.slopeIndex = slope;
        return s;
    }

    // Frequency response of a tap vector (DTFT at f).
    std::complex<double> response (const std::vector<float>& taps, double f)
    {
        std::complex<double> sum {};
        const auto w = 2.0 * pi * f / sampleRate;

        for (size_t n = 0; n < taps.size(); ++n)
            sum += (double) taps[n] * std::polar (1.0, -w * (double) n);

        return sum;
    }

    double db (std::complex<double> h) { return 20.0 * std::log10 (std::max (std::abs (h), 1.0e-12)); }

    std::complex<double> analogChain (const Bands& bands, double f)
    {
        std::complex<double> h { 1.0 };

        for (const auto& s : bands)
            if (s.enabled)
                h *= analogBandResponse (designAnalogBand (s, sampleRate), f, sampleRate);

        return h;
    }

    const std::vector<double> testFrequencies { 30, 50, 80, 120, 200, 350, 500, 800, 1000, 1500, 2500, 4000, 6000, 9000, 12000, 16000, 20000 };

    //==========================================================================
    void testLatency()
    {
        check (PhaseStage::latencyFor (PhaseMode::zeroLatency, 2, 48000.0) == 0, "zero latency mode has no latency");
        check (PhaseStage::latencyFor (PhaseMode::natural, 2, 48000.0) == 64 + 256, "natural phase latency is 320 samples at 48 kHz");
        check (PhaseStage::latencyFor (PhaseMode::linear, 2, 48000.0) == 4096 + 256, "linear phase (High) latency is half the kernel plus one partition");
        check (PhaseStage::latencyFor (PhaseMode::linear, 0, 44100.0) == 1024 + 256, "linear phase (Low) at 44.1 kHz");
        check (PhaseStage::latencyFor (PhaseMode::linear, 2, 96000.0) == 2 * (4096 + 256), "latency doubles at 96 kHz, keeping the same time");
        check (PhaseStage::latencyFor (PhaseMode::linear, 4, 192000.0) == 4 * (16384 + 256), "linear phase (Maximum) at 192 kHz");
    }

    void testLinearMatchesAnalog()
    {
        Bands bands {};
        bands[0] = band (FilterType::bell, 1000, 6, 1);
        bands[1] = band (FilterType::lowShelf, 150, -4, 0.7f);
        bands[2] = band (FilterType::highCut, 15000, 0, 1, Placement::stereo, 3);
        bands[3] = band (FilterType::bell, 120, 9, 2);

        const auto layout = makePhaseLayout (PhaseMode::linear, defaultLinearResolution, sampleRate);
        PhaseKernel k;
        designPhaseKernel (bands, layout, sampleRate, 2, k);

        auto symmetric = true;
        for (int i = 1; i < k.length; ++i)
            symmetric = symmetric && std::abs (k.taps[0][(size_t) i] - k.taps[0][(size_t) (k.length - i)]) < 1.0e-6f;

        check (symmetric, "linear phase kernel is symmetric");
        check (! k.crossTerms, "stereo bands need no left/right cross terms");

        double worst = 0.0, worstPhase = 0.0;

        for (auto f : testFrequencies)
        {
            const auto target = analogChain (bands, f);
            const auto h = response (k.taps[0], f);
            worst = std::max (worst, std::abs (db (h) - db (target)));

            // Remove the pure delay: what's left should have no phase shift.
            const auto centred = h * std::polar (1.0, 2.0 * pi * f / sampleRate * layout.kernelDelay);

            if (db (target) > -40.0)
                worstPhase = std::max (worstPhase, std::abs (std::arg (centred)) * 180.0 / pi);
        }

        check (worst < 0.02, "linear phase magnitude matches the analog curve within 0.02 dB (worst " + fmt (worst) + " dB)");
        check (worstPhase < 0.5, "linear phase has no phase shift beyond the delay (worst " + fmt (worstPhase) + " deg)");

        // Lower resolution trades accuracy in the lows for latency, but the
        // mids stay right.
        const auto low = makePhaseLayout (PhaseMode::linear, 0, sampleRate);
        designPhaseKernel (bands, low, sampleRate, 2, k);
        const auto midError = std::abs (db (response (k.taps[0], 1000)) - db (analogChain (bands, 1000)));
        check (midError < 0.1, "Low resolution still matches at 1 kHz (" + fmt (midError) + " dB)");
    }

    void testNaturalMatchesAnalog()
    {
        Bands bands {};
        bands[0] = band (FilterType::bell, 1000, 6, 1);
        bands[1] = band (FilterType::highShelf, 8000, 4, 0.7f);
        bands[2] = band (FilterType::lowCut, 80, 0, 1, Placement::stereo, 3);

        const auto layout = makePhaseLayout (PhaseMode::natural, 0, sampleRate);
        PhaseKernel k;
        designPhaseKernel (bands, layout, sampleRate, 2, k);

        double worstDb = 0.0, worstDeg = 0.0;

        for (auto f : testFrequencies)
        {
            const auto target = analogChain (bands, f);
            const auto h = response (k.taps[0], f) * std::polar (1.0, 2.0 * pi * f / sampleRate * layout.kernelDelay);
            worstDb = std::max (worstDb, std::abs (db (h) - db (target)));
            worstDeg = std::max (worstDeg, std::abs (std::arg (h / target)) * 180.0 / pi);
        }

        check (worstDb < 0.1, "natural phase magnitude matches the analog curve within 0.1 dB (worst " + fmt (worstDb) + " dB)");
        check (worstDeg < 2.0, "natural phase follows the analog phase within 2 degrees (worst " + fmt (worstDeg) + " deg)");
    }

    void testNoCramping()
    {
        Bands bands {};
        bands[0] = band (FilterType::bell, 16000, 12, 1.5f);

        const auto analogAt = [&] (double f) { return db (analogChain (bands, f)); };
        const auto digital = db (designResponse (designBand (bands[0], sampleRate), 21000, sampleRate));

        for (auto mode : { PhaseMode::natural, PhaseMode::linear })
        {
            const auto layout = makePhaseLayout (mode, defaultLinearResolution, sampleRate);
            PhaseKernel k;
            designPhaseKernel (bands, layout, sampleRate, 2, k);

            const auto name = std::string (mode == PhaseMode::natural ? "natural" : "linear");
            const auto at16 = db (response (k.taps[0], 16000));
            const auto at21 = db (response (k.taps[0], 21000));

            check (std::abs (at16 - 12.0) < 0.1, name + " phase: a 16 kHz bell peaks at its gain (" + fmt (at16) + " dB)");
            check (std::abs (at21 - analogAt (21000)) < 0.2, name + " phase: 21 kHz follows the analog curve ("
                                                              + fmt (at21) + " vs " + fmt (analogAt (21000)) + " dB)");
        }

        check (analogAt (21000) - digital > 1.0, "the zero latency IIR does cramp there (" + fmt (digital) + " dB at 21 kHz)");
    }

    void testMidSide()
    {
        Bands bands {};
        bands[0] = band (FilterType::bell, 2000, 8, 1, Placement::mid);
        bands[1] = band (FilterType::highShelf, 6000, -6, 0.7f, Placement::side);

        const auto layout = makePhaseLayout (PhaseMode::linear, defaultLinearResolution, sampleRate);
        PhaseKernel k;
        designPhaseKernel (bands, layout, sampleRate, 2, k);

        check (k.crossTerms, "mid/side bands mix left and right");

        // A mid signal (L = R) goes through LL + LR, a side signal (L = -R) through LL - LR.
        const auto mid  = [&] (double f) { return db (response (k.taps[0], f) + response (k.taps[1], f)); };
        const auto side = [&] (double f) { return db (response (k.taps[0], f) - response (k.taps[1], f)); };

        check (std::abs (mid (2000) - 8.0) < 0.1, "mid band boosts the mid by 8 dB (" + fmt (mid (2000)) + ")");
        check (std::abs (side (2000) - db (analogChain ({ bands[1] }, 2000))) < 0.1, "mid band leaves the side alone");
        check (std::abs (side (16000) + 6.0) < 0.2, "side shelf cuts the side by 6 dB (" + fmt (side (16000)) + ")");
        const auto midBell = [&] (double f) { return db (analogChain ({ bands[0] }, f)); };
        check (std::abs (mid (16000) - midBell (16000)) < 0.1, "side shelf leaves the mid alone");

        PhaseKernel mono;
        designPhaseKernel (bands, layout, sampleRate, 1, mono);
        check (std::abs (db (response (mono.taps[0], 2000)) - 8.0) < 0.1 && std::abs (db (response (mono.taps[0], 16000)) - midBell (16000)) < 0.1,
               "mono runs mid bands and skips side bands");
    }

    //==========================================================================
    // Runs a stereo signal through a stage block by block.
    std::vector<std::vector<float>> run (PhaseStage& stage, const std::vector<std::vector<float>>& in)
    {
        const auto length = (int) in[0].size();
        std::vector<std::vector<float>> out (2, std::vector<float> ((size_t) length));
        juce::AudioBuffer<float> buffer (2, blockSize);

        for (int start = 0; start < length; start += blockSize)
        {
            const auto n = std::min (blockSize, length - start);
            buffer.setSize (2, n, false, false, true);

            for (int ch = 0; ch < 2; ++ch)
                std::copy (in[(size_t) ch].begin() + start, in[(size_t) ch].begin() + start + n, buffer.getWritePointer (ch));

            stage.process (buffer);

            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out[(size_t) ch].begin() + start);
        }

        return out;
    }

    std::vector<std::vector<float>> noise (int length, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) length));

        for (auto& ch : x)
            for (auto& v : ch)
                v = dist (rng);

        return x;
    }

    void testStreamingEqualsDirect()
    {
        for (auto mode : { PhaseMode::natural, PhaseMode::linear })
        {
            Bands bands {};
            bands[0] = band (FilterType::bell, 500, 5, 2, Placement::left);
            bands[1] = band (FilterType::lowCut, 60, 0, 1, Placement::mid, 2);
            bands[2] = band (FilterType::highShelf, 5000, -3, 0.7f, Placement::stereo);

            const auto layout = makePhaseLayout (mode, 1, sampleRate);
            PhaseKernel k;
            designPhaseKernel (bands, layout, sampleRate, 2, k);

            PhaseStage stage;
            stage.setNonRealtime (true);
            stage.prepare (sampleRate, 2, mode, 1, bands);

            const auto length = layout.kernelLength + 3 * layout.partition + 1000;
            const auto x = noise (length, 7);
            const auto y = run (stage, x);

            double worst = 0.0;

            for (int out = 0; out < 2; ++out)
            {
                for (int n = 0; n < length; n += 7)
                {
                    // Direct convolution, delayed by one partition.
                    double expected = 0.0;
                    const auto t = n - layout.partition;

                    for (int in = 0; in < 2; ++in)
                    {
                        const auto& taps = k.taps[(size_t) (out * 2 + in)];
                        for (int j = 0; j < k.length && j <= t; ++j)
                            expected += (double) taps[(size_t) j] * x[(size_t) in][(size_t) (t - j)];
                    }

                    // The first partition fades the kernel in.
                    if (n >= 2 * layout.partition)
                        worst = std::max (worst, std::abs (expected - (double) y[(size_t) out][(size_t) n]));
                }
            }

            check (worst < 1.0e-4, std::string (mode == PhaseMode::natural ? "natural" : "linear")
                                       + " phase streaming equals direct convolution (worst " + std::to_string (worst) + ")");
        }
    }

    void testIdentityIsPureDelay()
    {
        PhaseStage stage;
        stage.setNonRealtime (true);
        stage.prepare (sampleRate, 2, PhaseMode::linear, 0, Bands {});

        const auto latency = stage.getLatency();
        const auto x = noise (latency + 4000, 3);
        const auto y = run (stage, x);

        double worst = 0.0;
        for (int n = latency + 512; n < (int) x[0].size(); ++n)
            worst = std::max (worst, (double) std::abs (y[0][(size_t) n] - x[0][(size_t) (n - latency)]));

        check (worst < 1.0e-5, "with no bands linear phase is a pure delay of the reported latency (worst " + std::to_string (worst) + ")");
    }

    // Largest jump between consecutive samples, relative to what a clean
    // sine of that frequency and amplitude can do.
    double worstStep (const std::vector<float>& y, int from, double freq, double amplitude)
    {
        double worst = 0.0;
        for (size_t n = (size_t) from + 1; n < y.size(); ++n)
            worst = std::max (worst, (double) std::abs (y[n] - y[n - 1]));
        return worst / (amplitude * 2.0 * std::sin (pi * freq / sampleRate));
    }

    std::vector<std::vector<float>> sine (int length, double freq, double amplitude)
    {
        std::vector<std::vector<float>> x (2, std::vector<float> ((size_t) length));
        for (int n = 0; n < length; ++n)
            x[0][(size_t) n] = x[1][(size_t) n] = (float) (amplitude * std::sin (2.0 * pi * freq * n / sampleRate));
        return x;
    }

    void testChangesDontClick()
    {
        // A gain change crossfades between kernels.
        PhaseStage stage;
        stage.setNonRealtime (true);

        Bands bands {};
        bands[0] = band (FilterType::bell, 1000, 0, 1);
        stage.prepare (sampleRate, 2, PhaseMode::linear, 1, bands);

        const auto x = sine ((int) sampleRate, 1000, 0.25);
        std::vector<std::vector<float>> y (2, std::vector<float> (x[0].size()));
        juce::AudioBuffer<float> buffer (2, blockSize);
        double peak = 0.0;

        for (int start = 0, block = 0; start + blockSize <= (int) x[0].size(); start += blockSize, ++block)
        {
            bands[0].gainDb = block < 50 ? 0.0f : 6.0f;
            stage.setBands (bands);

            for (int ch = 0; ch < 2; ++ch)
                std::copy (x[(size_t) ch].begin() + start, x[(size_t) ch].begin() + start + blockSize, buffer.getWritePointer (ch));

            stage.process (buffer);
            std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize, y[0].begin() + start);
            peak = std::max (peak, (double) buffer.getMagnitude (0, 0, blockSize));
        }

        const auto step = worstStep (y[0], stage.getLatency() + 512, 1000, 0.25 * std::pow (10.0, 6.0 / 20.0));
        check (step < 1.05, "a gain change crossfades without a click (worst step " + fmt (step) + "x a clean sine's)");
        check (std::abs (20.0 * std::log10 (peak / 0.25) - 6.0) < 0.1, "and lands on the new gain (" + fmt (20.0 * std::log10 (peak / 0.25)) + " dB)");

        // Mode and resolution switches fade out and back in.
        PhaseStage switcher;
        switcher.setNonRealtime (true);
        switcher.prepare (sampleRate, 2, PhaseMode::zeroLatency, 2, bands);

        const auto long_ = sine ((int) sampleRate * 2, 440, 0.5);
        juce::AudioBuffer<float> b (2, blockSize);
        double maxOut = 0.0;
        bool finite = true;
        double worst = 0.0;
        float last = 0.0f;

        const std::array<std::pair<PhaseMode, int>, 5> sequence { { { PhaseMode::linear, 2 }, { PhaseMode::natural, 0 },
                                                                    { PhaseMode::linear, 4 }, { PhaseMode::linear, 0 },
                                                                    { PhaseMode::zeroLatency, 0 } } };

        for (int start = 0, block = 0; start + blockSize <= (int) long_[0].size(); start += blockSize, ++block)
        {
            if (block % 40 == 0)
            {
                const auto& [mode, res] = sequence[(size_t) ((block / 40) % (int) sequence.size())];
                switcher.setTarget (mode, res);
            }

            switcher.setBands (bands);

            for (int ch = 0; ch < 2; ++ch)
                std::copy (long_[(size_t) ch].begin() + start, long_[(size_t) ch].begin() + start + blockSize, b.getWritePointer (ch));

            switcher.process (b);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = b.getSample (0, i);
                finite = finite && std::isfinite (v);
                maxOut = std::max (maxOut, (double) std::abs (v));
                worst = std::max (worst, (double) std::abs (v - last));
                last = v;
            }
        }

        const auto cleanStep = 0.5 * 2.0 * std::sin (pi * 440.0 / sampleRate);
        check (finite && maxOut < 0.5 * 1.2, "switching modes stays finite and bounded (peak " + fmt (maxOut) + ")");
        check (worst < 1.2 * cleanStep, "switching modes doesn't click (worst step " + fmt (worst / cleanStep) + "x a clean sine's)");
    }

    void testBackgroundDesign()
    {
        // Live, kernels come from the designer thread a little later.
        PhaseStage stage;
        stage.setNonRealtime (false);

        Bands bands {};
        stage.prepare (sampleRate, 2, PhaseMode::natural, 0, bands);
        bands[0] = band (FilterType::bell, 1000, -12, 1);

        const auto x = sine (blockSize, 1000, 0.5);
        juce::AudioBuffer<float> buffer (2, blockSize);
        double level = 1.0;

        for (int block = 0; block < 400 && level > 0.2; ++block)
        {
            stage.setBands (bands);

            for (int ch = 0; ch < 2; ++ch)
                std::copy (x[(size_t) ch].begin(), x[(size_t) ch].end(), buffer.getWritePointer (ch));

            stage.process (buffer);

            if (block > 4)
                level = buffer.getMagnitude (0, 0, blockSize);

            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }

        check (level < 0.2, "a kernel designed in the background takes over (level " + fmt (level) + ", expect ~0.126)");
    }

    // Not a pass/fail check: how much of one core the heaviest settings use.
    void reportCpu()
    {
        for (auto [mode, res] : { std::pair { PhaseMode::natural, 0 }, std::pair { PhaseMode::linear, 2 }, std::pair { PhaseMode::linear, 4 } })
        {
            Bands bands {};
            bands[0] = band (FilterType::bell, 300, 4, 1, Placement::mid);
            bands[1] = band (FilterType::highShelf, 8000, 3, 0.7f, Placement::side);

            PhaseStage stage;
            stage.setNonRealtime (true);
            stage.prepare (sampleRate, 2, mode, res, bands);

            const auto seconds = 5;
            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            const auto start = std::chrono::steady_clock::now();

            for (int i = 0; i < seconds * (int) sampleRate / 512; ++i)
                stage.process (buffer);

            const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            std::printf ("info: %s, mid/side, 48 kHz stereo: %.1f%% of one core\n",
                         mode == PhaseMode::natural ? "natural" : res == 2 ? "linear (High)" : "linear (Maximum)",
                         100.0 * elapsed / seconds);
        }
    }
}

int main()
{
    testLatency();
    testLinearMatchesAnalog();
    testNaturalMatchesAnalog();
    testNoCramping();
    testMidSide();
    testStreamingEqualsDirect();
    testIdentityIsPureDelay();
    testChangesDontClick();
    testBackgroundDesign();
    reportCpu();

    std::printf ("\n%s\n", failures == 0 ? "All phase mode tests passed." : "Some phase mode tests FAILED.");
    return failures == 0 ? 0 : 1;
}
