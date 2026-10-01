#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EqModel.h"

namespace fabcutie::ui
{
    // Compact controls for one band that float over the graph next to the
    // selected node: type, slope, placement, and frequency / gain / Q knobs,
    // with the band's dynamics (threshold, range, attack, release and the
    // sidechain settings) on a second row.
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

        // Relabels the placement choices for a surround bus.
        void setSurround (bool surround);

        static constexpr int preferredWidth  = 420;
        static constexpr int preferredHeight = 222;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
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

        juce::TextButton dynamicButton { "DYN" };
        std::unique_ptr<ButtonAttachment> dynamicAttachment;
        juce::ComboBox sourceBox, detectorFilterBox;
        std::unique_ptr<ComboBoxAttachment> sourceAttachment, detectorFilterAttachment;
        Knob threshold, range, attack, release;
        float shownDynamicGainDb = 0.0f;

        std::array<Knob*, 7> allKnobs() noexcept { return { &frequency, &gain, &q, &threshold, &range, &attack, &release }; }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
    };
}
