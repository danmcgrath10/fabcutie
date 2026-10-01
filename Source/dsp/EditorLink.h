#pragma once

#include "AudioTap.h"
#include "PeakMeter.h"

namespace fabcutie::dsp
{
    // Everything the audio thread shares with the editor that is not a
    // parameter: the analyzer feeds, the output meter and the band solo.
    struct EditorLink
    {
        AudioTap pre;      // input, before the EQ
        AudioTap post;     // output, after the EQ and output gain
        AudioTap external; // the sidechain input, when the host connects one

        PeakMeter outputMeter;

        // Set by the editor while an analyzer is showing, so the audio thread
        // does not fill the taps for nobody.
        std::atomic<bool> analyzerActive { false };

        // True while the host feeds the sidechain input.
        std::atomic<bool> sidechainConnected { false };

        // The band being soloed, or -1. Not saved: solo is for listening.
        std::atomic<int> soloBand { -1 };
    };
}
