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

        // EQ Match listens on its own taps (the analyzer drains the others):
        // the input before the EQ, and the sidechain as the reference.
        AudioTap matchSource;
        AudioTap matchReference;
        std::atomic<bool> matchLearning { false };

        PeakMeter outputMeter;

        // How many editors are showing an analyzer of this instance, so the
        // audio thread does not fill the taps for nobody.
        std::atomic<int> analyzerUsers { 0 };

        // True while the host feeds the sidechain input.
        std::atomic<bool> sidechainConnected { false };

        // Channels on the main bus (more than two in surround).
        std::atomic<int> mainChannels { 2 };

        // The band being soloed, or -1. Not saved: solo is for listening.
        std::atomic<int> soloBand { -1 };
    };
}
