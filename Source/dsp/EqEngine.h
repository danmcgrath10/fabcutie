#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "EqBand.h"

namespace fabcutie::dsp
{
    // Runs up to 24 bands in series. Each band works on the stereo pair, one
    // side of it, or the mid or side signal; the buffer is converted between
    // left/right and mid/side only when consecutive bands need it.
    class EqEngine
    {
    public:
        void prepare (double sampleRate)
        {
            for (auto& band : bands)
                band.prepare (sampleRate);
        }

        void setBand (int index, const BandSettings& settings) noexcept
        {
            bands[(size_t) index].setTarget (settings);
        }

        void process (juce::AudioBuffer<float>& buffer) noexcept
        {
            const auto numChannels = std::min (buffer.getNumChannels(), EqBand::maxChannels);
            const auto numSamples = buffer.getNumSamples();

            if (numChannels == 0 || numSamples == 0)
                return;

            auto* const* data = buffer.getArrayOfWritePointers();
            auto midSide = false;

            for (auto& band : bands)
            {
                const auto placement = band.beginBlock();

                if (! band.isActive())
                    continue;

                float* channels[2] {};
                int slots[2] {};
                int count = 0;

                auto use = [&] (int channel)
                {
                    channels[count] = data[channel];
                    slots[count] = channel;
                    ++count;
                };

                if (numChannels == 1)
                {
                    // A mono signal is all mid and no side.
                    if (placement != Placement::side)
                        use (0);
                }
                else
                {
                    const auto wantsMidSide = placement == Placement::mid || placement == Placement::side;

                    if (wantsMidSide != midSide)
                    {
                        convert (data, numSamples, wantsMidSide);
                        midSide = wantsMidSide;
                    }

                    switch (placement)
                    {
                        case Placement::stereo: use (0); use (1); break;
                        case Placement::left:
                        case Placement::mid:    use (0); break;
                        case Placement::right:
                        case Placement::side:   use (1); break;
                    }
                }

                if (count > 0)
                    band.process (channels, slots, count, numSamples);
            }

            if (midSide)
                convert (data, numSamples, false);
        }

    private:
        // L/R -> M/S uses M = (L + R) / 2, S = (L - R) / 2, so the way back is
        // L = M + S, R = M - S (exact apart from float rounding).
        static void convert (float* const* data, int numSamples, bool toMidSide) noexcept
        {
            auto* a = data[0];
            auto* b = data[1];

            for (int i = 0; i < numSamples; ++i)
            {
                const auto x = a[i], y = b[i];

                if (toMidSide) { a[i] = 0.5f * (x + y); b[i] = 0.5f * (x - y); }
                else           { a[i] = x + y;          b[i] = x - y; }
            }
        }

        std::array<EqBand, maxBands> bands;
    };
}
