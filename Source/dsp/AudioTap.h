#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace fabcutie::dsp
{
    // Hands a mono mix of the audio to the editor without locking: the audio
    // thread pushes, the message thread pulls. When the editor falls behind
    // (or is closed), new samples that do not fit are simply dropped.
    class AudioTap
    {
    public:
        static constexpr int capacity = 1 << 15;

        AudioTap() : buffer ((size_t) capacity, 0.0f) {}

        // Audio thread. Averages the channels into one signal.
        void push (const float* const* channels, int numChannels, int numSamples) noexcept
        {
            if (numChannels <= 0 || numSamples <= 0)
                return;

            const auto scale = 1.0f / (float) numChannels;
            const auto scope = fifo.write (std::min (numSamples, fifo.getFreeSpace()));

            auto copy = [&] (int start, int count, int offset)
            {
                for (int i = 0; i < count; ++i)
                {
                    auto sum = 0.0f;
                    for (int c = 0; c < numChannels; ++c)
                        sum += channels[c][offset + i];

                    buffer[(size_t) (start + i)] = sum * scale;
                }
            };

            copy (scope.startIndex1, scope.blockSize1, 0);
            copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
        }

        // Message thread. Returns how many samples were copied into dest.
        int pull (float* dest, int maxSamples) noexcept
        {
            const auto scope = fifo.read (std::min (maxSamples, fifo.getNumReady()));

            std::copy_n (buffer.data() + scope.startIndex1, scope.blockSize1, dest);
            std::copy_n (buffer.data() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);

            return scope.blockSize1 + scope.blockSize2;
        }

        int getNumReady() const noexcept { return fifo.getNumReady(); }

    private:
        juce::AbstractFifo fifo { capacity };
        std::vector<float> buffer;
    };
}
