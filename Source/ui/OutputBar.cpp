#include "OutputBar.h"
#include "Parameters.h"
#include "Theme.h"

namespace fabcutie::ui
{
    OutputBar::OutputBar (juce::AudioProcessorValueTreeState& state, std::function<float()> source)
        : autoGainDb (std::move (source))
    {
        gainScale.setTooltip ("Gain scale: scales the gain of every band (100 % leaves them as set, 0 % flattens, "
                              "negative inverts). Double-click for 100 %");
        gainScale.setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.35f));
        gainScale.setColour (juce::Slider::backgroundColourId, colours::control);
        gainScale.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        gainScale.setDoubleClickReturnValue (true, 100.0);
        gainScale.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 20); // a bar draws its value inside
        addAndMakeVisible (gainScale);

        autoGainButton.setClickingTogglesState (true);
        autoGainButton.setTooltip ("Auto gain: offsets the output by the average level change of the bells, shelves and tilts");
        addAndMakeVisible (autoGainButton);

        invertButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x98"))); // Ø
        invertButton.setClickingTogglesState (true);
        invertButton.setTooltip ("Phase invert: flip the output polarity");
        addAndMakeVisible (invertButton);

        for (auto* b : { &autoGainButton, &invertButton })
        {
            b->setColour (juce::TextButton::buttonOnColourId, colours::accent);
            b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        }

        gainScaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::id::gainScale, gainScale);
        autoGainAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::id::autoGain, autoGainButton);
        invertAttachment    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::id::phaseInvert, invertButton);

        startTimerHz (10);
    }

    OutputBar::~OutputBar() = default;

    void OutputBar::timerCallback()
    {
        const auto db = autoGainButton.getToggleState() && autoGainDb ? autoGainDb() : 0.0f;

        if (std::abs (db - shownAutoGainDb) >= 0.05f)
        {
            shownAutoGainDb = db;
            autoGainButton.setButtonText (std::abs (db) < 0.05f ? "Auto gain"
                                                                : (db > 0.0f ? "Auto +" : "Auto ") + juce::String (db, 1) + " dB");
        }
    }

    void OutputBar::paint (juce::Graphics& g)
    {
        // Continues the analyzer bar's strip.
        g.setColour (colours::background);
        g.fillRect (getLocalBounds());
        g.setColour (colours::panelOutline.withMultipliedAlpha (0.5f));
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("SCALE", gainScale.getBounds().withX (gainScale.getX() - 44).withWidth (40), juce::Justification::centredRight);
    }

    void OutputBar::resized()
    {
        auto area = getLocalBounds().reduced (12, 4);

        invertButton.setBounds (area.removeFromRight (30));
        area.removeFromRight (6);
        autoGainButton.setBounds (area.removeFromRight (104));
        area.removeFromRight (10);

        area.removeFromLeft (44); // "SCALE"
        gainScale.setBounds (area.reduced (0, 2));
    }
}
