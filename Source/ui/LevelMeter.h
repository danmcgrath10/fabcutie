#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/PeakMeter.h"

namespace fabcutie::ui
{
    // Output level meter: a bar per channel with a short peak hold, and the
    // highest peak so far printed on top (red once it passes 0 dBFS). Click
    // the readout to reset it.
    class LevelMeter final : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::Timer
    {
    public:
        explicit LevelMeter (dsp::PeakMeter&);

        static constexpr float maxDb = 6.0f;
        static constexpr float minDb = -60.0f;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        // Maps a level to 0 (bottom) .. 1 (top). Spends more of the height
        // on the top 20 dB, where mixing decisions happen.
        static float proportionForDb (float db) noexcept;

    private:
        struct Channel
        {
            float levelDb = minDb;
            float holdDb = minDb;
            double holdUntil = 0.0;
        };

        void timerCallback() override;
        juce::Rectangle<float> readoutArea() const;
        juce::Rectangle<float> barArea() const;

        dsp::PeakMeter& meter;
        std::array<Channel, dsp::PeakMeter::maxChannels> channels;
        int numChannels = 2;
        float maxPeakDb = -std::numeric_limits<float>::infinity();
        double lastTick = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
    };
}
