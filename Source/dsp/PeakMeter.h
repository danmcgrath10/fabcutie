#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace fabcutie::dsp
{
    // Collects the highest absolute sample per channel on the audio thread
    // until the editor reads (and clears) it.
    class PeakMeter
    {
    public:
        static constexpr int maxChannels = 2;

        // Audio thread.
        void process (const juce::AudioBuffer<float>& buffer, int numChannels) noexcept
        {
            numChannels = std::min (numChannels, maxChannels);
            channelCount.store (numChannels, std::memory_order_relaxed);

            for (int c = 0; c < numChannels; ++c)
            {
                const auto blockPeak = buffer.getMagnitude (c, 0, buffer.getNumSamples());
                auto& p = peaks[(size_t) c];
                if (blockPeak > p.load (std::memory_order_relaxed))
                    p.store (blockPeak, std::memory_order_relaxed);
            }
        }

        // Message thread: the peak since the last call, as linear gain.
        float takePeak (int channel) noexcept
        {
            return peaks[(size_t) channel].exchange (0.0f, std::memory_order_relaxed);
        }

        int getNumChannels() const noexcept { return channelCount.load (std::memory_order_relaxed); }

    private:
        std::array<std::atomic<float>, maxChannels> peaks {};
        std::atomic<int> channelCount { 2 };
    };
}
