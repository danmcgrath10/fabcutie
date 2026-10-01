#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Colours and the look and feel shared by every editor component.
namespace fabcutie::ui
{
    namespace colours
    {
        inline const juce::Colour background   { 0xff0f1115 };
        inline const juce::Colour graphTop     { 0xff171a21 };
        inline const juce::Colour graphBottom  { 0xff0c0e12 };
        inline const juce::Colour gridMajor    { 0x26ffffff };
        inline const juce::Colour gridMinor    { 0x0fffffff };
        inline const juce::Colour gridText     { 0x66ffffff };
        inline const juce::Colour text         { 0xffe6e8ee };
        inline const juce::Colour textDim      { 0x99e6e8ee };
        inline const juce::Colour panel        { 0xf01b1e26 };
        inline const juce::Colour panelOutline { 0x33ffffff };
        inline const juce::Colour control      { 0xff2a2e38 };
        inline const juce::Colour accent       { 0xffff7aa8 };
        inline const juce::Colour curve        { 0xfff4f5f8 };
    }

    // Each band gets its own hue, stepped round the colour wheel by the golden
    // ratio so neighbouring band numbers never look alike.
    inline juce::Colour bandColour (int bandIndex)
    {
        const auto hue = std::fmod (0.93f + 0.618034f * (float) bandIndex, 1.0f);
        return juce::Colour::fromHSV (hue, 0.62f, 0.98f, 1.0f);
    }

    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOver, bool isButtonDown) override;

        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
    };
}
