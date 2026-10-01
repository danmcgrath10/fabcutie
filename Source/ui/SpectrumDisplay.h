#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AnalyzerSettings.h"
#include "EqGraph.h"
#include "dsp/EditorLink.h"
#include "dsp/SpectrumAnalyzer.h"

namespace fabcutie::ui
{
    // The real-time spectrum behind the EQ curves: the input (pre), the
    // output (post) and the sidechain (external) signals, plus red collision
    // shading where the output and the sidechain fight over the same
    // frequencies. Sits in EqGraph as its background layer and offers the
    // graph peaks to grab.
    class SpectrumDisplay final : public juce::Component,
                                  public EqGraph::PeakSource,
                                  private juce::Timer
    {
    public:
        SpectrumDisplay (EqGraph&, dsp::EditorLink&, std::function<double()> sampleRateSource);
        ~SpectrumDisplay() override;

        void setSettings (const AnalyzerSettings&);

        std::optional<juce::Point<float>> findPeakNear (juce::Point<float>) override;
        void setHighlightedPeak (std::optional<juce::Point<float>>) override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Trace
        {
            Trace (dsp::AudioTap& t, juce::Colour c) : tap (t), colour (c) {}

            dsp::AudioTap& tap;
            juce::Colour colour;
            dsp::SpectrumAnalyzer analyzer;
            std::vector<float> db; // per display point, tilt applied
            bool visible = false;
            double lastUpdate = 0.0; // ms, when the levels last moved
        };

        void timerCallback() override;
        void rebuildPoints();
        void resetClocks();
        void updateTrace (Trace&);
        float yForLevel (float db) const noexcept;
        juce::Path tracePath (const Trace&, bool closed) const;
        void drawCollisions (juce::Graphics&);
        void drawHighlight (juce::Graphics&);

        EqGraph& graph;
        dsp::EditorLink& link;
        std::function<double()> sampleRateSource;
        AnalyzerSettings settings;

        Trace pre, post, external;
        std::vector<float> scratch;

        std::vector<float> pointX, pointLowHz, pointHighHz;
        GraphGeometry lastGeometry;
        juce::Rectangle<float> plot;

        std::optional<juce::Point<float>> highlight;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
    };
}
