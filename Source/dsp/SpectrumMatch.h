#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace fabcutie::dsp
{
    // EQ Match: learns the long-term average spectrum of a signal (the track
    // being mixed, or the reference it should sound like) and works out the
    // EQ curve that moves one towards the other. CurveFit then turns that
    // curve into bands.
    //
    // Runs on the message thread, fed from an AudioTap.
    class LongTermSpectrum
    {
    public:
        static constexpr int order = 13; // 8192 points: ~6 Hz bins at 48 kHz
        static constexpr int size = 1 << order;
        static constexpr int hop = size / 2;

        LongTermSpectrum() : fft (order), window ((size_t) size), history ((size_t) size, 0.0f),
                             fftData ((size_t) size * 2, 0.0f), power ((size_t) size / 2 + 1, 0.0)
        {
            juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) size,
                                                                      juce::dsp::WindowingFunction<float>::hann, false);

            // A sine of amplitude A reads A in its bin, like the analyzer.
            auto sum = 0.0f;
            for (auto w : window) sum += w;
            scale = 2.0f / sum;
        }

        void reset()
        {
            std::fill (history.begin(), history.end(), 0.0f);
            std::fill (power.begin(), power.end(), 0.0);
            frames = 0;
            filled = 0;
            pending = 0;
            writePos = 0;
        }

        void push (const float* samples, int numSamples)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                history[(size_t) writePos] = samples[i];
                writePos = (writePos + 1) % size;
                filled = std::min (filled + 1, size);

                if (++pending >= hop && filled == size)
                {
                    pending = 0;
                    analyse();
                }
            }
        }

        int getNumFrames() const noexcept { return frames; }
        bool hasData() const noexcept { return frames > 0; }

        // Seconds of audio learned so far.
        double getSeconds (double sampleRate) const noexcept
        {
            return frames > 0 ? (double) (size + (frames - 1) * hop) / sampleRate : 0.0;
        }

        // Average level in dB at each frequency, with the power averaged over
        // `octaves` around it, so single harmonics do not dominate.
        std::vector<double> levelsDb (const std::vector<double>& hz, double sampleRate, double octaves = 1.0 / 3.0) const
        {
            std::vector<double> out (hz.size(), -200.0);
            if (frames == 0)
                return out;

            const auto binHz = sampleRate / size;
            const auto lastBin = (int) power.size() - 1;
            const auto halfWidth = std::pow (2.0, 0.5 * octaves);

            for (size_t i = 0; i < hz.size(); ++i)
            {
                // At least the nearest bin, so the lowest frequencies are never empty.
                const auto centre = std::clamp ((int) std::lround (hz[i] / binHz), 1, lastBin);
                const auto first = std::clamp ((int) std::ceil (hz[i] / halfWidth / binHz), 1, centre);
                const auto last = std::clamp ((int) std::floor (hz[i] * halfWidth / binHz), centre, lastBin);

                auto sum = 0.0;
                for (auto k = first; k <= last; ++k)
                    sum += power[(size_t) k];

                const auto mean = sum / (double) (last - first + 1) / (double) frames;
                out[i] = 10.0 * std::log10 (std::max (mean, 1.0e-20));
            }

            return out;
        }

        // Average level of everything between lowHz and highHz, in dB: the
        // power of the bins added up, so a sine of amplitude A inside the
        // range reads about 20 log10 A, like the analyzer.
        double rangeLevelDb (double lowHz, double highHz, double sampleRate) const
        {
            if (frames == 0)
                return -200.0;

            const auto binHz = sampleRate / size;
            const auto lastBin = (int) power.size() - 1;
            const auto first = std::clamp ((int) std::ceil (lowHz / binHz), 1, lastBin);
            const auto last = std::clamp ((int) std::floor (highHz / binHz), first, lastBin);

            auto sum = 0.0;
            for (auto k = first; k <= last; ++k)
                sum += power[(size_t) k];

            // The Hann window spreads a sine over 1.5 bins' worth of power.
            return 10.0 * std::log10 (std::max (sum / (double) frames / 1.5, 1.0e-20));
        }

    private:
        void analyse()
        {
            for (int i = 0; i < size; ++i)
                fftData[(size_t) i] = history[(size_t) ((writePos + i) % size)] * window[(size_t) i];

            std::fill (fftData.begin() + size, fftData.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            for (size_t k = 0; k < power.size(); ++k)
            {
                const auto m = (double) (fftData[k] * scale);
                power[k] += m * m;
            }

            ++frames;
        }

        juce::dsp::FFT fft;
        std::vector<float> window, history, fftData;
        std::vector<double> power;
        float scale = 1.0f;
        int frames = 0, filled = 0, pending = 0, writePos = 0;
    };

    namespace match
    {
        // The EQ curve (dB per frequency) that moves the source's tonal
        // balance to the reference's. The overall level difference is taken
        // out, so matching changes tone, not loudness. Frequencies where
        // either signal is close to silent are left alone. `amount` scales
        // the result (1 = a full match) and maxDb limits it.
        inline std::vector<double> curve (const std::vector<double>& hz, const std::vector<double>& sourceDb,
                                          const std::vector<double>& referenceDb, double amount = 1.0,
                                          double maxDb = 18.0, double floorDb = -100.0)
        {
            const auto n = hz.size();
            std::vector<double> diff (n, 0.0);
            std::vector<bool> valid (n, false);

            auto sum = 0.0;
            auto count = 0;

            for (size_t i = 0; i < n; ++i)
            {
                valid[i] = sourceDb[i] > floorDb && referenceDb[i] > floorDb;
                if (! valid[i])
                    continue;

                diff[i] = referenceDb[i] - sourceDb[i];

                if (hz[i] >= 40.0 && hz[i] <= 16000.0)
                {
                    sum += diff[i];
                    ++count;
                }
            }

            const auto offset = count > 0 ? sum / count : 0.0;

            // Smooth over a third of an octave either side, so the bands go
            // after the tonal balance rather than the measurement's ripple.
            std::vector<double> smooth (n, 0.0);

            for (size_t i = 0; i < n; ++i)
            {
                if (! valid[i])
                    continue;

                auto total = 0.0, weight = 0.0;

                for (size_t j = 0; j < n; ++j)
                {
                    const auto octaves = std::abs (std::log2 (hz[j] / hz[i]));
                    if (! valid[j] || octaves > 1.0 / 3.0)
                        continue;

                    const auto w = 1.0 - 3.0 * octaves;
                    total += w * diff[j];
                    weight += w;
                }

                smooth[i] = std::clamp ((total / weight - offset) * amount, -maxDb, maxDb);
            }

            return smooth;
        }
    }
}
