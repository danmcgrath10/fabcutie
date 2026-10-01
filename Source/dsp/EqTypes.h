#pragma once

#include <array>

namespace fabcutie::dsp
{
    inline constexpr int maxBands = 24;

    // Order matters: these indices are stored in saved sessions through the
    // band type parameter. Only append new types at the end.
    enum class FilterType
    {
        bell,
        lowShelf,
        lowCut,
        highShelf,
        highCut,
        notch,
        bandPass,
        tiltShelf,
        allPass,  // flat magnitude, shifts phase around the frequency
        flatTilt  // a straight dB/octave tilt across the whole spectrum
    };

    inline constexpr int numFilterTypes = 10;

    // Which part of the signal a band works on. Also stored in sessions.
    enum class Placement
    {
        stereo,
        left,
        right,
        mid,
        side
    };

    inline constexpr int numPlacements = 5;

    // Cut slopes in dB/octave. A slope of 6 * n dB/oct is an order n filter.
    inline constexpr std::array<int, 9> cutSlopesDbPerOct { 6, 12, 18, 24, 30, 36, 48, 72, 96 };
    inline constexpr int maxFilterOrder = 16;

    // The slope choice after the fixed slopes: a 16th-order inverse
    // Chebyshev cut that is flat up to the cutoff and 100 dB down less than
    // half an octave past it.
    inline constexpr int brickwallSlopeIndex = (int) cutSlopesDbPerOct.size();
    inline constexpr int numSlopes = brickwallSlopeIndex + 1;

    inline constexpr bool isBrickwall (int slopeIndex) noexcept
    {
        return slopeIndex >= brickwallSlopeIndex;
    }

    inline constexpr int filterOrderForSlope (int slopeIndex) noexcept
    {
        if (slopeIndex < 0) slopeIndex = 0;
        if (slopeIndex >= (int) cutSlopesDbPerOct.size()) return maxFilterOrder;
        return cutSlopesDbPerOct[(size_t) slopeIndex] / 6;
    }

    struct BandSettings
    {
        bool enabled = false;
        FilterType type = FilterType::bell;
        float frequency = 1000.0f; // Hz
        float gainDb = 0.0f;       // bell, shelves and tilts
        float q = 1.0f;            // bandwidth; for cuts, 1 is a flat (Butterworth) knee
        int slopeIndex = 1;        // into cutSlopesDbPerOct (or brickwallSlopeIndex), used by the cut types
        Placement placement = Placement::stereo;

        // Changing any of these swaps the filter structure, so the band
        // briefly fades its effect out and back in instead of gliding.
        bool sameStructure (const BandSettings& other) const noexcept
        {
            return enabled == other.enabled && type == other.type
                && slopeIndex == other.slopeIndex && placement == other.placement;
        }
    };
}
