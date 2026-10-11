#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>
#include <cstring>

namespace fabcutie::dsp
{
    // NaN and infinity checks that still work under -ffast-math (where
    // std::isfinite may be folded to true): the exponent bits are all ones.
    inline bool isFiniteSample (float x) noexcept
    {
        std::uint32_t bits;
        std::memcpy (&bits, &x, sizeof bits);
        return (bits & 0x7f800000u) != 0x7f800000u;
    }

    // Replaces NaN and infinite samples with silence. A single one reaching
    // a filter would stay in its state for good and silence the track, so
    // every input is cleaned before it is processed. Returns true when it
    // found any.
    inline bool scrubNonFinite (juce::AudioBuffer<float>& buffer) noexcept
    {
        auto found = false;

        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            auto* data = buffer.getWritePointer (c);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (! isFiniteSample (data[i]))
                {
                    data[i] = 0.0f;
                    found = true;
                }
            }
        }

        return found;
    }
}
