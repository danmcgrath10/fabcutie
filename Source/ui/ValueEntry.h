#pragma once

#include "EqModel.h"

namespace fabcutie::ui
{
    // A small box over the editor for typing a band's frequency, gain and Q.
    // Frequencies take Hz, "k" for kHz ("2.5k") or a note name ("A4").
    namespace ValueEntry
    {
        void show (juce::Component& parent, EqModel&, int band);
    }
}
