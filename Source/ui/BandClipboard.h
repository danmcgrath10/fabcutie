#pragma once

#include "EqModel.h"

namespace fabcutie::ui
{
    // Copy and paste of band settings through the system clipboard, as a
    // small XML document, so bands can move between FabCutie instances.
    namespace BandClipboard
    {
        // Copies the settings of the given bands (in that order).
        void copy (const EqModel&, const juce::Array<int>& bands);

        // How many bands the clipboard holds (0 if it holds none).
        int count();

        // Pastes every copied band into free band slots. Returns the bands it
        // filled, fewer than copied when the slots run out.
        juce::Array<int> pasteAsNew (EqModel&);

        // Pastes the first copied band's settings onto an existing band.
        bool pasteOnto (EqModel&, int band);
    }
}
