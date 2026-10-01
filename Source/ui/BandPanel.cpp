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

        sourceBox.addItemList (params::detectorSourceNames(), 1);
        detectorFilterBox.addItemList (params::detectorFilterNames(), 1);

        sourceBox.setTooltip ("Sidechain: the band's own input, or the plugin's sidechain input");
        detectorFilterBox.setTooltip ("Sidechain filter: listen only around the band, or to the whole signal");

        for (auto* box : { &typeBox, &slopeBox, &placementBox, &sourceBox, &detectorFilterBox })
            addAndMakeVisible (*box);

        dynamicButton.setClickingTogglesState (true);
        dynamicButton.setTooltip ("Dynamic: move the band's gain with the level of the signal");
        addAndMakeVisible (dynamicButton);

        frequency.name = "FREQ";
        gain.name = "GAIN";
        q.name = "Q";
        threshold.name = "THRESH";
        range.name = "RANGE";
        attack.name = "ATTACK";
        release.name = "RELEASE";

        for (auto* knob : allKnobs())
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

        soloButton.setButtonText ("Solo");
        soloButton.setTooltip ("Listen to just the part of the spectrum this band works on");
        soloButton.setClickingTogglesState (true);
        soloButton.setColour (juce::TextButton::buttonOnColourId, colours::solo);
        soloButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        soloButton.onClick = [this]
        {
            if (soloTarget != nullptr && band >= 0)
                soloTarget->store (soloButton.getToggleState() ? band : -1);
        };
        addAndMakeVisible (soloButton);

        startTimerHz (15);
    }

    void BandPanel::setSurround (bool surround)
    {
        const auto names = EqModel::placementNames (surround);

        for (int i = 0; i < names.size(); ++i)
            placementBox.changeItemText (i + 1, names[i]);

        placementBox.setTooltip (surround ? "Which speakers the band works on" : "Which channels the band works on");
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
        dynamicAttachment.reset();
        sourceAttachment.reset();
        detectorFilterAttachment.reset();

        for (auto* knob : allKnobs())
            knob->attachment.reset();

        if (band < 0)
            return;

        auto& state = model.getState();
        const auto id = [this] (BandParam p) { return params::bandParamId (band, p); };

        typeAttachment      = std::make_unique<ComboBoxAttachment> (state, id (BandParam::type), typeBox);
        slopeAttachment     = std::make_unique<ComboBoxAttachment> (state, id (BandParam::slope), slopeBox);
        placementAttachment = std::make_unique<ComboBoxAttachment> (state, id (BandParam::placement), placementBox);
        sourceAttachment    = std::make_unique<ComboBoxAttachment> (state, id (BandParam::detectorSource), sourceBox);
        detectorFilterAttachment = std::make_unique<ComboBoxAttachment> (state, id (BandParam::detectorFilter), detectorFilterBox);
        dynamicAttachment   = std::make_unique<ButtonAttachment> (state, id (BandParam::dynamic), dynamicButton);

        const std::pair<Knob*, BandParam> knobs[] { { &frequency, BandParam::frequency },
                                                     { &gain, BandParam::gain },
                                                     { &q, BandParam::q },
                                                     { &threshold, BandParam::threshold },
                                                     { &range, BandParam::range },
                                                     { &attack, BandParam::attack },
                                                     { &release, BandParam::release } };

        const auto colour = bandColour (band);

        for (auto [knob, p] : knobs)
        {
            knob->attachment = std::make_unique<SliderAttachment> (state, id (p), knob->slider);

            auto& param = model.parameter (band, p);
            knob->slider.setDoubleClickReturnValue (true, param.convertFrom0to1 (param.getDefaultValue()));
            knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        }

        updateEnablement();
        timerCallback();
        repaint();
    }

    void BandPanel::timerCallback()
    {
        updateEnablement();

        const auto soloed = soloTarget != nullptr && band >= 0 && soloTarget->load() == band;
        soloButton.setToggleState (soloed, juce::dontSendNotification);
        // Keep the live dynamic gain readout in the header current.
        const auto dynamicGain = band >= 0 ? model.getDynamicGainDb (band) : 0.0f;
        if (std::abs (dynamicGain - shownDynamicGainDb) >= 0.05f)
        {
            shownDynamicGainDb = dynamicGain;
            repaint (getLocalBounds().removeFromTop (34));
        }
    }

    void BandPanel::updateEnablement()
    {
        if (band < 0)
            return;

        const auto settings = model.getBand (band);
        const auto type = settings.type;
        gain.slider.setEnabled (EqModel::usesGain (type));
        q.slider.setEnabled (EqModel::usesQ (settings));
        slopeBox.setEnabled (EqModel::usesSlope (type));

        const auto canBeDynamic = EqModel::usesDynamics (type);
        const auto dynamic = canBeDynamic && settings.dynamics.enabled;
        dynamicButton.setEnabled (canBeDynamic);

        auto changed = false;
        for (juce::Component* c : { (juce::Component*) &sourceBox, (juce::Component*) &detectorFilterBox,
                                    (juce::Component*) &threshold.slider, (juce::Component*) &range.slider,
                                    (juce::Component*) &attack.slider, (juce::Component*) &release.slider })
        {
            changed = changed || c->isEnabled() != dynamic;
            c->setEnabled (dynamic);
        }

        if (changed)
            repaint();
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

        if (std::abs (shownDynamicGainDb) >= 0.05f)
        {
            g.setColour (colour);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ((shownDynamicGainDb > 0.0f ? "+" : "") + juce::String (shownDynamicGainDb, 1) + " dB",
                        header.withTrimmedRight (30.0f), juce::Justification::centredRight);
        }

        // A hairline between the EQ row and the dynamics row.
        g.setColour (colours::panelOutline);
        g.fillRect (juce::Rectangle<float> (12.0f, (float) dynamicButton.getY() - 7.0f, (float) getWidth() - 24.0f, 1.0f));

        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        for (auto* knob : allKnobs())
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
        header.removeFromRight (6);
        soloButton.setBounds (header.removeFromRight (48).reduced (0, 1));

        area.removeFromTop (6);

        const auto rowHeight = 3 * 22 + 2 * 6;
        auto eqRow = area.removeFromTop (rowHeight);
        area.removeFromTop (14);
        auto dynamicRow = area.removeFromTop (rowHeight);

        auto layoutRow = [] (juce::Rectangle<int> row, std::initializer_list<juce::Component*> column,
                             std::initializer_list<Knob*> knobs)
        {
            auto left = row.removeFromLeft (128);
            for (auto* c : column)
            {
                c->setBounds (left.removeFromTop (22));
                left.removeFromTop (6);
            }

            row.removeFromLeft (8);
            row.removeFromTop (14); // knob names

            const auto knobWidth = row.getWidth() / (int) knobs.size();
            for (auto* knob : knobs)
                knob->slider.setBounds (row.removeFromLeft (knobWidth));
        };

        layoutRow (eqRow, { &typeBox, &slopeBox, &placementBox }, { &frequency, &gain, &q });
        layoutRow (dynamicRow, { &dynamicButton, &sourceBox, &detectorFilterBox }, { &threshold, &range, &attack, &release });

        for (auto* knob : { &threshold, &range, &attack, &release })
            knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knob->slider.getWidth(), 16);
    }
}
