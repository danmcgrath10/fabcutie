#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EqModel.h"
#include "dsp/EditorLink.h"
#include "dsp/SpectrumMatch.h"

namespace fabcutie::ui
{
    // EQ Match: learns the average spectrum of the input and of a reference,
    // previews the EQ curve that moves one towards the other, and turns it
    // into bands.
    //
    // The reference is either the sidechain input (learned at the same time
    // as the input), or a capture: play the reference through the plugin
    // first and press Capture, then play your own track and press Learn.
    // The capture is saved with the session.
    class MatchPanel final : public juce::Component,
                             private juce::Timer
    {
    public:
        MatchPanel (EqModel&, dsp::EditorLink&, std::function<double()> sampleRateSource);
        ~MatchPanel() override;

        static constexpr int preferredWidth  = 330;
        static constexpr int preferredHeight = 250;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

    private:
        enum class Listening { nothing, capture, learn };

        void timerCallback() override;
        void setListening (Listening);
        void updateCurve();
        void apply();
        void reset();
        double getSampleRate() const;

        bool usesCapture() const noexcept { return referenceBox.getSelectedItemIndex() == 1; }
        bool hasReference() const noexcept;

        void loadCapture();
        void saveCapture();

        EqModel& model;
        dsp::EditorLink& link;
        std::function<double()> sampleRateSource;

        const std::vector<double> hz;
        dsp::LongTermSpectrum source, sidechain, capture;
        std::vector<double> capturedDb; // the saved reference, at hz
        std::vector<double> curve;      // the match curve, at hz
        bool curveValid = false;
        std::vector<float> scratch;

        Listening listening = Listening::nothing;
        int timerTicks = 0;

        juce::ComboBox referenceBox;
        juce::TextButton captureButton { "Capture" }, learnButton { "Learn" }, resetButton { "Reset" }, applyButton { "Apply" };
        juce::Slider amount { juce::Slider::LinearBar, juce::Slider::TextBoxAbove };
        juce::Slider bandCount { juce::Slider::LinearBar, juce::Slider::TextBoxAbove };

        juce::Rectangle<int> previewArea, statusArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchPanel)
    };
}
