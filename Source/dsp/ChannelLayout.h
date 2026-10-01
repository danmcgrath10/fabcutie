#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>

namespace fabcutie::dsp
{
    // Most channels the plug-in processes: 9.1.6, the largest layout offered.
    inline constexpr int maxChannels = 16;

    // Which side of the listener a speaker sits on. In surround, a band's
    // placement picks speakers by side instead of matrixing mid/side.
    enum class ChannelSide { left, right, centre };

    // The speaker roles of the main bus, set up when playback starts.
    struct ChannelMap
    {
        int numChannels = 2;
        std::array<ChannelSide, maxChannels> sides { ChannelSide::left, ChannelSide::right };

        // More than two channels: placements select speakers by side.
        bool isSurround() const noexcept { return numChannels > 2; }

        static ChannelSide sideOf (juce::AudioChannelSet::ChannelType type) noexcept
        {
            using CT = juce::AudioChannelSet::ChannelType;

            static constexpr CT leftSide[] { CT::left, CT::leftSurround, CT::leftCentre, CT::leftSurroundSide,
                                             CT::topFrontLeft, CT::topRearLeft, CT::leftSurroundRear, CT::wideLeft,
                                             CT::topSideLeft, CT::bottomFrontLeft, CT::proximityLeft,
                                             CT::bottomSideLeft, CT::bottomRearLeft };

            static constexpr CT rightSide[] { CT::right, CT::rightSurround, CT::rightCentre, CT::rightSurroundSide,
                                              CT::topFrontRight, CT::topRearRight, CT::rightSurroundRear, CT::wideRight,
                                              CT::topSideRight, CT::bottomFrontRight, CT::proximityRight,
                                              CT::bottomSideRight, CT::bottomRearRight };

            if (std::find (std::begin (leftSide), std::end (leftSide), type) != std::end (leftSide))
                return ChannelSide::left;

            if (std::find (std::begin (rightSide), std::end (rightSide), type) != std::end (rightSide))
                return ChannelSide::right;

            // Centre, LFE, centre surround, the top and bottom centres.
            return ChannelSide::centre;
        }

        static ChannelMap fromLayout (const juce::AudioChannelSet& layout)
        {
            ChannelMap map;
            map.numChannels = juce::jlimit (0, maxChannels, layout.size());

            for (int c = 0; c < map.numChannels; ++c)
                map.sides[(size_t) c] = sideOf (layout.getTypeOfChannel (c));

            return map;
        }
    };

    // The main bus layouts offered besides mono and stereo, up to 9.1.6.
    inline bool isSupportedSurroundLayout (const juce::AudioChannelSet& set)
    {
        return set.size() > 2 && set.size() <= maxChannels
            && ! set.isDiscreteLayout() && set.getAmbisonicOrder() < 0;
    }
}
