#include "Theme.h"

namespace fabcutie::ui
{
    LookAndFeel::LookAndFeel()
    {
        setColourScheme ({ colours::background, colours::panel, colours::control,
                           colours::panelOutline, colours::text, colours::accent,
                           colours::background, colours::control, colours::text });

        setColour (juce::ComboBox::backgroundColourId, colours::control);
        setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::arrowColourId, colours::textDim);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d2029));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedTextColourId, colours::text);
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.4f));
        setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, colours::control);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::TextButton::buttonColourId, colours::control);
        setColour (juce::TextButton::buttonOnColourId, colours::accent);
        setColour (juce::TextButton::textColourOffId, colours::textDim);
        setColour (juce::TextButton::textColourOnId, colours::background);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1d2029));
        setColour (juce::TooltipWindow::textColourId, colours::text);
    }

    void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto track  = juce::jmax (2.5f, radius * 0.14f);
        const auto arcRadius = radius - track * 0.5f;
        const auto angle  = startAngle + sliderPos * (endAngle - startAngle);
        const auto fill   = slider.findColour (juce::Slider::rotarySliderFillColourId)
                                .withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.35f);

        juce::Path background;
        background.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::control);
        g.strokePath (background, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Bipolar parameters (like gain) fill outwards from the middle.
        const auto range = slider.getRange();
        const auto bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
        const auto fromAngle = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                                       : startAngle;

        if (std::abs (angle - fromAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
            g.setColour (fill);
            g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const auto knobRadius = arcRadius - track * 1.4f;
        g.setColour (juce::Colour (0xff232731));
        g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

        const auto tip = centre.getPointOnCircumference (knobRadius * 0.85f, angle);
        const auto inner = centre.getPointOnCircumference (knobRadius * 0.35f, angle);
        g.setColour (slider.isEnabled() ? colours::text : colours::textDim.withMultipliedAlpha (0.5f));
        g.drawLine ({ inner, tip }, juce::jmax (1.5f, radius * 0.08f));
    }

    void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox& box)
    {
        auto bounds = juce::Rectangle<int> (width, height).toFloat();
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId)
                         .withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f)
                         .brighter (box.isMouseOver() ? 0.12f : 0.0f));
        g.fillRoundedRectangle (bounds, 4.0f);

        const auto arrowArea = bounds.removeFromRight ((float) height).reduced ((float) height * 0.36f);
        juce::Path arrow;
        arrow.startNewSubPath (arrowArea.getX(), arrowArea.getY() + arrowArea.getHeight() * 0.3f);
        arrow.lineTo (arrowArea.getCentreX(), arrowArea.getBottom() - arrowArea.getHeight() * 0.2f);
        arrow.lineTo (arrowArea.getRight(), arrowArea.getY() + arrowArea.getHeight() * 0.3f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool isMouseOver, bool isButtonDown)
    {
        auto colour = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;
        if (isButtonDown)     colour = colour.brighter (0.2f);
        else if (isMouseOver) colour = colour.brighter (0.1f);

        g.setColour (colour);
        g.fillRoundedRectangle (button.getLocalBounds().toFloat(), 4.0f);
    }

    juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)
    {
        return juce::Font (juce::FontOptions (13.0f));
    }

    juce::Font LookAndFeel::getPopupMenuFont()
    {
        return juce::Font (juce::FontOptions (14.0f));
    }

    juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (juce::Font (juce::FontOptions (12.5f)));
        return label;
    }
}
