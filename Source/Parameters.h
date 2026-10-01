#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Central place for every automatable parameter. The EQ engine will add its
// per-band parameters here (see the band section stub below) so that IDs stay
// stable across versions and sessions saved in Logic keep loading.
namespace fabcutie::params
{
    // Bump when a parameter's meaning changes, never reuse an old ID.
    inline constexpr int version = 1;

    namespace id
    {
        inline constexpr auto outputGain = "outputGain";
        inline constexpr auto bypass     = "bypass";
    }

    namespace range
    {
        inline constexpr float gainMinDb = -36.0f;
        inline constexpr float gainMaxDb =  36.0f;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
