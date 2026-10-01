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
        tiltShelf
    };

    inline constexpr int numFilterTypes = 8;

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

    inline constexpr int filterOrderForSlope (int slopeIndex) noexcept
    {
        if (slopeIndex < 0) slopeIndex = 0;
        if (slopeIndex >= (int) cutSlopesDbPerOct.size()) slopeIndex = (int) cutSlopesDbPerOct.size() - 1;
        return cutSlopesDbPerOct[(size_t) slopeIndex] / 6;
    }

    struct BandSettings
    {
        bool enabled = false;
        FilterType type = FilterType::bell;
        float frequency = 1000.0f; // Hz
        float gainDb = 0.0f;       // bell, shelves and tilt
        float q = 1.0f;            // bandwidth; for cuts, 1 is a flat (Butterworth) knee
        int slopeIndex = 1;        // into cutSlopesDbPerOct, used by the cut types
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
