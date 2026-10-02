#include "AnalyzerBar.h"
#include "Theme.h"

namespace fabcutie::ui
{
    AnalyzerBar::AnalyzerBar (std::function<bool()> connected)
    {
        preButton.setTooltip ("Show the input spectrum, before the EQ");
        postButton.setTooltip ("Show the output spectrum, after the EQ");
        freezeButton.setTooltip ("Hold the spectrum still");
        menuButton.setTooltip ("Analyzer range, speed, tilt, resolution and more");

        for (auto* button : { &preButton, &postButton, &externalButton, &freezeButton })
        {
            button->setClickingTogglesState (true);
            button->setColour (juce::TextButton::buttonOnColourId, colours::control.brighter (0.35f));
            button->onClick = [this] { changed(); };
            addAndMakeVisible (*button);
        }

        menuButton.onClick = [this] { showMenu(); };
        addAndMakeVisible (menuButton);

        syncButtons();

        // Only from here on: the owner may still be under construction (the
        // editor builds this bar before the members the callback reads).
        sidechainConnected = std::move (connected);
        startTimerHz (4);
    }

    void AnalyzerBar::setSettings (const AnalyzerSettings& newSettings)
    {
        settings = newSettings;
        syncButtons();
    }

    void AnalyzerBar::syncButtons()
    {
        preButton.setToggleState (settings.showPre, juce::dontSendNotification);
        postButton.setToggleState (settings.showPost, juce::dontSendNotification);
        externalButton.setToggleState (settings.showExternal, juce::dontSendNotification);
        freezeButton.setToggleState (settings.freeze, juce::dontSendNotification);
        timerCallback();
    }

    void AnalyzerBar::timerCallback()
    {
        // The sidechain spectrum only exists while the host feeds the
        // sidechain input; say so instead of silently showing nothing.
        const auto connected = sidechainConnected && sidechainConnected();
        externalButton.setAlpha (connected ? 1.0f : 0.55f);
        externalButton.setTooltip (connected ? "Show the sidechain spectrum, and where it collides with the output"
                                             : "Show the sidechain spectrum. Route a signal to FabCutie's sidechain input in your DAW to use it.");
    }

    void AnalyzerBar::changed()
    {
        settings.showPre = preButton.getToggleState();
        settings.showPost = postButton.getToggleState();
        settings.showExternal = externalButton.getToggleState();
        settings.freeze = freezeButton.getToggleState();

        if (onChange)
            onChange (settings);
    }

    void AnalyzerBar::showMenu()
    {
        juce::PopupMenu menu;

        juce::PopupMenu range;
        for (size_t i = 0; i < AnalyzerSettings::rangesDb.size(); ++i)
            range.addItem (100 + (int) i, juce::String ((int) AnalyzerSettings::rangesDb[i]) + " dB", true, settings.rangeIndex == (int) i);
        menu.addSubMenu ("Range", range);

        juce::PopupMenu speed;
        for (int i = 0; i < AnalyzerSettings::speedNames.size(); ++i)
            speed.addItem (200 + i, AnalyzerSettings::speedNames[i], true, settings.speedIndex == i);
        menu.addSubMenu ("Speed", speed);

        juce::PopupMenu tilt;
        for (size_t i = 0; i < AnalyzerSettings::tiltsDbPerOct.size(); ++i)
            tilt.addItem (300 + (int) i, juce::String (AnalyzerSettings::tiltsDbPerOct[i], 1) + " dB/oct", true, settings.tiltIndex == (int) i);
        menu.addSubMenu ("Tilt", tilt);

        juce::PopupMenu resolution;
        for (int i = 0; i < AnalyzerSettings::resolutionNames.size(); ++i)
            resolution.addItem (400 + i, AnalyzerSettings::resolutionNames[i]
                                             + " (" + juce::String (1 << AnalyzerSettings::fftOrders[(size_t) i]) + " points)",
                                true, settings.resolutionIndex == i);
        menu.addSubMenu ("Resolution", resolution);

        menu.addSeparator();
        menu.addItem (1, "Show collisions with sidechain", true, settings.collisions);
        menu.addItem (2, "Spectrum grab", true, settings.spectrumGrab);

        juce::Component::SafePointer<AnalyzerBar> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton),
                            [safeThis] (int result)
                            {
                                if (safeThis == nullptr || result == 0)
                                    return;

                                auto& s = safeThis->settings;

                                if (result == 1)        s.collisions = ! s.collisions;
                                else if (result == 2)   s.spectrumGrab = ! s.spectrumGrab;
                                else if (result >= 400) s.resolutionIndex = result - 400;
                                else if (result >= 300) s.tiltIndex = result - 300;
                                else if (result >= 200) s.speedIndex = result - 200;
                                else if (result >= 100) s.rangeIndex = result - 100;

                                if (safeThis->onChange)
                                    safeThis->onChange (s);
                            });
    }

    void AnalyzerBar::paint (juce::Graphics& g)
    {
        g.setColour (colours::background);
        g.fillRect (getLocalBounds());
        g.setColour (colours::panelOutline.withMultipliedAlpha (0.5f));
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    }

    void AnalyzerBar::resized()
    {
        auto area = getLocalBounds().reduced (12, 4);

        menuButton.setBounds (area.removeFromLeft (84));
        area.removeFromLeft (10);

        const std::pair<juce::TextButton*, int> toggles[] { { &preButton, 48 }, { &postButton, 52 }, { &externalButton, 78 } };

        for (auto [button, width] : toggles)
        {
            button->setBounds (area.removeFromLeft (width));
            area.removeFromLeft (4);
        }

        area.removeFromLeft (10);
        freezeButton.setBounds (area.removeFromLeft (62));
    }
}
