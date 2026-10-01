#pragma once

#include <cmath>
#include <string>

namespace fabcutie::dsp
{
    // Equal-tempered note maths for the piano roll: MIDI note numbers with
    // A4 = note 69 = 440 Hz, and C4 as middle C.
    inline constexpr double referenceHz = 440.0;
    inline constexpr int referenceNote = 69;

    inline double noteForFrequency (double hz) noexcept
    {
        return referenceNote + 12.0 * std::log2 (hz / referenceHz);
    }

    inline double frequencyForNote (double note) noexcept
    {
        return referenceHz * std::exp2 ((note - referenceNote) / 12.0);
    }

    // The nearest note's exact frequency.
    inline double snapToNote (double hz) noexcept
    {
        return frequencyForNote (std::round (noteForFrequency (hz)));
    }

    inline bool isBlackKey (int note) noexcept
    {
        const auto pitchClass = ((note % 12) + 12) % 12;
        return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
    }

    // "A4", "C#2", "F#-1"...
    inline std::string noteName (int note)
    {
        static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto pitchClass = ((note % 12) + 12) % 12;
        const auto octave = (note - pitchClass) / 12 - 1;
        return std::string (names[pitchClass]) + std::to_string (octave);
    }
}
