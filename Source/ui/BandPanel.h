#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EqModel.h"

namespace fabcutie::ui
{
    // Compact controls for one band that float over the graph next to the
    // selected node: type, slope, placement, and frequency / gain / Q knobs.
    class BandPanel final : public juce::Component,
                            private juce::Timer
    {
    public:
        explicit BandPanel (EqModel&);
        ~BandPanel() override;

        void setBand (int band);
        int getBand() const noexcept { return band; }

        // Where the soloed band is kept (shared with the audio thread).
        void setSoloTarget (std::atomic<int>* target) noexcept { soloTarget = target; }

        static constexpr int preferredWidth  = 420;
        static constexpr int preferredHeight = 128;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        using BandParam = params::BandParam;

        struct Knob
        {
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
            juce::String name;
            std::unique_ptr<SliderAttachment> attachment;
        };

        void timerCallback() override;
        void updateEnablement();

        EqModel& model;
        int band = -1;

        juce::ComboBox typeBox, slopeBox, placementBox;
        std::unique_ptr<ComboBoxAttachment> typeAttachment, slopeAttachment, placementAttachment;

        Knob frequency, gain, q;
        juce::TextButton removeButton, soloButton;
        std::atomic<int>* soloTarget = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
    };
}
