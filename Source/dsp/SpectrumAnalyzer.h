#pragma once

#include <juce_dsp/juce_dsp.h>

namespace fabcutie::dsp
{
    // Turns a stream of samples into a smoothed magnitude spectrum in dBFS.
    // Runs on the message thread (fed from an AudioTap). A full-scale sine
    // reads 0 dB at its frequency.
    class SpectrumAnalyzer
    {
    public:
        static constexpr int minOrder = 11; // 2048-point FFT
        static constexpr int maxOrder = 14; // 16384-point FFT
        static constexpr float floorDb = -160.0f;

        explicit SpectrumAnalyzer (int order = 13) { setOrder (order); }

        void setOrder (int order)
        {
            order = juce::jlimit (minOrder, maxOrder, order);

            if (fft != nullptr && fft->getSize() == 1 << order)
                return;

            fft = std::make_unique<juce::dsp::FFT> (order);
            const auto size = fft->getSize();

            window.resize ((size_t) size);
            juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) size,
                                                                      juce::dsp::WindowingFunction<float>::hann, false);

            // Scale so a sine of amplitude A peaks at A: its energy lands in one
            // bin with magnitude A * sum(window) / 2.
            auto sum = 0.0f;
            for (auto w : window) sum += w;
            magnitudeScale = 2.0f / sum;

            history.assign ((size_t) size, 0.0f);
            fftData.assign ((size_t) size * 2, 0.0f);
            levels.assign ((size_t) size / 2 + 1, floorDb);
            writePos = 0;
            pending = 0;
        }

        int getFftSize() const noexcept { return fft->getSize(); }

        // New samples are analysed every hop, so the display updates at a
        // steady rate whatever the resolution.
        int getHopSize() const noexcept { return std::min (1024, getFftSize() / 4); }

        void setSampleRate (double newSampleRate) noexcept { sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0; }
        double getSampleRate() const noexcept { return sampleRate; }

        void reset()
        {
            std::fill (history.begin(), history.end(), 0.0f);
            std::fill (levels.begin(), levels.end(), floorDb);
            pending = 0;
        }

        void push (const float* samples, int numSamples) noexcept
        {
            const auto size = (int) history.size();

            for (int i = 0; i < numSamples; ++i)
            {
                history[(size_t) writePos] = samples[i];
                writePos = (writePos + 1) % size;
            }

            pending = std::min (pending + numSamples, size);
        }

        // Analyses the newest samples if at least a hop has arrived, and moves
        // the smoothed levels towards them: rises almost at once, falls with
        // the given release time (seconds to drop by about 63% in dB).
        // elapsedSeconds is the time since the previous call. Returns true when
        // the levels changed.
        bool update (double elapsedSeconds, float releaseSeconds)
        {
            if (pending < getHopSize())
                return false;

            pending = 0;
            analyse();

            const auto dt = (float) juce::jlimit (0.001, 0.5, elapsedSeconds);
            const auto attack = 1.0f - std::exp (-dt / attackSeconds);
            const auto release = 1.0f - std::exp (-dt / std::max (0.01f, releaseSeconds));

            for (size_t k = 0; k < levels.size(); ++k)
            {
                const auto db = fftData[k];
                auto& level = levels[k];
                level += (db - level) * (db > level ? attack : release);
            }

            return true;
        }

        // Smoothed level in dB over a frequency span. Spans narrower than a
        // bin are interpolated between bins; wider ones take the loudest bin,
        // so narrow peaks at high frequencies stay visible.
        float levelForSpan (float lowHz, float highHz) const noexcept
        {
            const auto binHz = (float) sampleRate / (float) getFftSize();
            const auto lastBin = (int) levels.size() - 1;

            const auto b0 = lowHz / binHz;
            const auto b1 = highHz / binHz;

            if (b1 - b0 < 1.0f)
            {
                const auto centre = juce::jlimit (0.0f, (float) lastBin, std::sqrt (std::max (lowHz, 1.0f) * highHz) / binHz);
                const auto i = std::min ((int) centre, lastBin - 1);
                const auto t = centre - (float) i;
                return levels[(size_t) i] + t * (levels[(size_t) i + 1] - levels[(size_t) i]);
            }

            const auto first = juce::jlimit (0, lastBin, (int) std::ceil (b0));
            const auto last = juce::jlimit (0, lastBin, (int) std::floor (b1));

            auto loudest = floorDb;
            for (auto i = first; i <= last; ++i)
                loudest = std::max (loudest, levels[(size_t) i]);

            return loudest;
        }

        const std::vector<float>& getLevels() const noexcept { return levels; }

    private:
        static constexpr float attackSeconds = 0.012f;

        void analyse()
        {
            const auto size = (int) history.size();

            // Oldest sample first.
            for (int i = 0; i < size; ++i)
                fftData[(size_t) i] = history[(size_t) ((writePos + i) % size)] * window[(size_t) i];

            std::fill (fftData.begin() + size, fftData.end(), 0.0f);
            fft->performFrequencyOnlyForwardTransform (fftData.data(), true);

            for (size_t k = 0; k < levels.size(); ++k)
                fftData[k] = juce::jmax (floorDb, juce::Decibels::gainToDecibels (fftData[k] * magnitudeScale, floorDb));
        }

        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, history, fftData, levels;
        float magnitudeScale = 1.0f;
        double sampleRate = 48000.0;
        int writePos = 0, pending = 0;
    };
}
