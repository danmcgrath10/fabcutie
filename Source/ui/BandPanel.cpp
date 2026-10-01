#include "BandPanel.h"
#include "Theme.h"

namespace fabcutie::ui
{
    BandPanel::BandPanel (EqModel& m) : model (m)
    {
        // ComboBoxAttachment maps choice index i to item id i + 1.
        typeBox.addItemList (params::filterTypeNames(), 1);
        slopeBox.addItemList (params::slopeNames(), 1);
        placementBox.addItemList (params::placementNames(), 1);

        typeBox.setTooltip ("Filter type");
        slopeBox.setTooltip ("Cut slope");
        placementBox.setTooltip ("Which channels the band works on");

        for (auto* box : { &typeBox, &slopeBox, &placementBox })
            addAndMakeVisible (*box);

        frequency.name = "FREQ";
        gain.name = "GAIN";
        q.name = "Q";

        for (auto* knob : { &frequency, &gain, &q })
        {
            knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
            addAndMakeVisible (knob->slider);
        }

        removeButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")));
        removeButton.setTooltip ("Remove band");
        removeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        removeButton.onClick = [this]
        {
            if (band >= 0)
                model.removeBand (band);
        };
        addAndMakeVisible (removeButton);

        startTimerHz (15);
    }

    BandPanel::~BandPanel()
    {
        // Attachments must go before the controls they are attached to.
        setBand (-1);
    }

    void BandPanel::setBand (int newBand)
    {
        if (newBand == band)
            return;

        band = newBand;

        typeAttachment.reset();
        slopeAttachment.reset();
        placementAttachment.reset();

        for (auto* knob : { &frequency, &gain, &q })
            knob->attachment.reset();

        if (band < 0)
            return;

        auto& state = model.getState();
        const auto id = [this] (BandParam p) { return params::bandParamId (band, p); };

        typeAttachment      = std::make_unique<ComboBoxAttachment> (state, id (BandParam::type), typeBox);
        slopeAttachment     = std::make_unique<ComboBoxAttachment> (state, id (BandParam::slope), slopeBox);
        placementAttachment = std::make_unique<ComboBoxAttachment> (state, id (BandParam::placement), placementBox);

        const std::pair<Knob*, BandParam> knobs[] { { &frequency, BandParam::frequency },
                                                     { &gain, BandParam::gain },
                                                     { &q, BandParam::q } };

        const auto colour = bandColour (band);

        for (auto [knob, p] : knobs)
        {
            knob->attachment = std::make_unique<SliderAttachment> (state, id (p), knob->slider);

            auto& param = model.parameter (band, p);
            knob->slider.setDoubleClickReturnValue (true, param.convertFrom0to1 (param.getDefaultValue()));
            knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        }

        updateEnablement();
        repaint();
    }

    void BandPanel::timerCallback()
    {
        updateEnablement();
    }

    void BandPanel::updateEnablement()
    {
        if (band < 0)
            return;

        const auto settings = model.getBand (band);
        gain.slider.setEnabled (EqModel::usesGain (settings.type));
        q.slider.setEnabled (EqModel::usesQ (settings));
        slopeBox.setEnabled (EqModel::usesSlope (settings.type));
    }

    void BandPanel::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto colour = band >= 0 ? bandColour (band) : colours::accent;

        g.setColour (colours::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (colours::panelOutline);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        // A strip of the band's colour along the top edge.
        g.setColour (colour);
        g.fillRoundedRectangle (bounds.withHeight (3.0f).reduced (10.0f, 0.0f), 1.5f);

        if (band < 0)
            return;

        auto header = getLocalBounds().reduced (12, 8).removeFromTop (22).toFloat();
        g.fillEllipse (header.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f));
        header.removeFromLeft (8.0f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("Band " + juce::String (band + 1), header, juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        for (auto* knob : { &frequency, &gain, &q })
        {
            g.setColour (knob->slider.isEnabled() ? colours::textDim : colours::textDim.withMultipliedAlpha (0.4f));
            const auto area = knob->slider.getBounds().withHeight (14).translated (0, -14);
            g.drawText (knob->name, area, juce::Justification::centred);
        }
    }

    void BandPanel::resized()
    {
        auto area = getLocalBounds().reduced (12, 8);

        auto header = area.removeFromTop (22);
        removeButton.setBounds (header.removeFromRight (24));

        area.removeFromTop (6);

        auto left = area.removeFromLeft (128);
        for (auto* box : { &typeBox, &slopeBox, &placementBox })
        {
            box->setBounds (left.removeFromTop (22));
            left.removeFromTop (6);
        }

        area.removeFromLeft (8);
        area.removeFromTop (14); // knob names

        const auto knobWidth = area.getWidth() / 3;
        for (auto* knob : { &frequency, &gain, &q })
            knob->slider.setBounds (area.removeFromLeft (knobWidth));
    }
}
