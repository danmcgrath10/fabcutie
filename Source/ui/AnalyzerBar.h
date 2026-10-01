#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AnalyzerSettings.h"

namespace fabcutie::ui
{
    // The strip under the graph: quick toggles for the pre, post and
    // sidechain spectra and freeze, plus a menu with the analyzer's range,
    // speed, tilt, resolution, collision and spectrum grab settings.
    class AnalyzerBar final : public juce::Component,
                              private juce::Timer
    {
    public:
        explicit AnalyzerBar (std::function<bool()> sidechainConnected);

        void setSettings (const AnalyzerSettings&);
        const AnalyzerSettings& getSettings() const noexcept { return settings; }

        std::function<void (const AnalyzerSettings&)> onChange;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;
        void changed();
        void showMenu();
        void syncButtons();

        std::function<bool()> sidechainConnected;
        AnalyzerSettings settings;

        juce::TextButton menuButton { "Analyzer" };
        juce::TextButton preButton { "Pre" }, postButton { "Post" }, externalButton { "Sidechain" }, freezeButton { "Freeze" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerBar)
    };
}
