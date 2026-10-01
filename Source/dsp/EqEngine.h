#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "ChannelLayout.h"
#include "EqBand.h"

namespace fabcutie::dsp
{
    // Runs up to 24 bands in series. Each band works on the stereo pair, one
    // side of it, or the mid or side signal; the buffer is converted between
    // left/right and mid/side only when consecutive bands need it. Dynamic
    // bands set to an external source listen to the sidechain buffer.
    //
    // In surround (more than two channels) there is no mid/side matrix:
    // placements pick speakers by where they sit. Stereo is every channel,
    // Left and Right the speakers on that side, Mid the centre line (centre,
    // LFE, centre surround and the top/bottom centres) and Side every speaker
    // off the centre line.
    class EqEngine
    {
    public:
        void prepare (double sampleRate)
        {
            for (auto& band : bands)
                band.prepare (sampleRate);
        }

        // Message thread, before prepare(): the speaker roles of the main bus.
        void setChannelMap (const ChannelMap& map) noexcept { channelMap = map; }

        // Whether a speaker on the given side takes part in a placement, in surround.
        static bool surroundUses (Placement placement, ChannelSide side) noexcept
        {
            switch (placement)
            {
                case Placement::stereo: return true;
                case Placement::left:   return side == ChannelSide::left;
                case Placement::right:  return side == ChannelSide::right;
                case Placement::mid:    return side == ChannelSide::centre;
                case Placement::side:   return side != ChannelSide::centre;
            }

            return false;
        }

        void setBand (int index, const BandSettings& settings) noexcept
        {
            bands[(size_t) index].setTarget (settings);
        }

        float getDynamicGainDb (int index) const noexcept
        {
            return bands[(size_t) index].getDynamicGainDb();
        }

        void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain = nullptr) noexcept
        {
            const auto numChannels = std::min (buffer.getNumChannels(), EqBand::maxChannels);
            const auto numSamples = buffer.getNumSamples();

            if (numChannels == 0 || numSamples == 0)
                return;

            auto* const* data = buffer.getArrayOfWritePointers();
            auto midSide = false;

            // With no sidechain connected, external detection hears silence.
            const auto numSidechain = sidechain != nullptr ? std::min (sidechain->getNumChannels(), BandDynamics::maxChannels) : 0;
            const auto sidechainSamples = sidechain != nullptr ? std::min (sidechain->getNumSamples(), numSamples) : 0;
            const float* const* sidechainData = numSidechain > 0 && sidechainSamples == numSamples
                                                    ? sidechain->getArrayOfReadPointers() : nullptr;
            static const float* const noChannels[1] { nullptr };

            for (auto& band : bands)
            {
                const auto placement = band.beginBlock();

                if (! band.isActive())
                    continue;

                float* channels[EqBand::maxChannels] {};
                int slots[EqBand::maxChannels] {};
                int count = 0;

                auto use = [&] (int channel)
                {
                    channels[count] = data[channel];
                    slots[count] = channel;
                    ++count;
                };

                if (numChannels > 2)
                {
                    for (int c = 0; c < numChannels; ++c)
                    {
                        const auto side = c < channelMap.numChannels ? channelMap.sides[(size_t) c] : ChannelSide::centre;

                        if (surroundUses (placement, side))
                            use (c);
                    }
                }
                else if (numChannels == 1)
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
                {
                    const auto external = band.listensToSidechain();

                    if (! external)
                        band.process (channels, slots, count, numSamples);
                    else if (sidechainData != nullptr)
                        band.process (channels, slots, count, numSamples, sidechainData, numSidechain);
                    else
                        band.process (channels, slots, count, numSamples, noChannels, 0);
                }
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
        ChannelMap channelMap;
    };
}
