#pragma once

#include <algorithm>
#include <cmath>

namespace fabcutie::ui
{
    // Maps between the graph's pixels and frequency (log scale) / gain (dB).
    // Plain maths with no JUCE types, so it can be unit tested.
    struct GraphGeometry
    {
        float x = 0.0f, y = 0.0f, width = 1.0f, height = 1.0f;

        float minHz = 10.0f;
        float maxHz = 30000.0f;
        float rangeDb = 12.0f; // the graph shows +/- this many dB

        float xForFrequency (float hz) const noexcept
        {
            return x + width * std::log (std::max (hz, 1.0e-3f) / minHz) / std::log (maxHz / minHz);
        }

        float frequencyForX (float px) const noexcept
        {
            return minHz * std::pow (maxHz / minHz, (px - x) / width);
        }

        float yForDb (float db) const noexcept
        {
            return y + height * 0.5f * (1.0f - db / rangeDb);
        }

        float dbForY (float py) const noexcept
        {
            return rangeDb * (1.0f - 2.0f * (py - y) / height);
        }
    };
}
