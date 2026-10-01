#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace fabcutie::ui
{
    // Global output options under the graph, beside the analyzer bar: gain
    // scale, auto gain (with the offset it is applying) and phase invert.
    class OutputBar final : public juce::Component,
                            private juce::Timer
    {
    public:
        OutputBar (juce::AudioProcessorValueTreeState&, std::function<float()> autoGainDbSource);
        ~OutputBar() override;

        juce::Slider& getGainScaleSlider() noexcept { return gainScale; }

        static constexpr int preferredWidth = 330;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        std::function<float()> autoGainDb;
        float shownAutoGainDb = 0.0f;

        juce::Slider gainScale { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
        juce::TextButton autoGainButton { "Auto gain" }, invertButton;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainScaleAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment, invertAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputBar)
    };
}
