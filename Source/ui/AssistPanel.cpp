#include "AssistPanel.h"
#include "Theme.h"
#include "dsp/CurveFit.h"

namespace fabcutie::ui
{
    namespace
    {
        constexpr float previewRangeDb = 60.0f;

        // Starting points for the bands Assist adds.
        constexpr float resonanceAttackMs = 5.0f, resonanceReleaseMs = 80.0f, maxResonanceCutDb = 12.0f;
        constexpr float unmaskAttackMs = 5.0f, unmaskReleaseMs = 150.0f;

        // Unmask bands start ducking a little below the key's usual level
        // in their band, so they work whenever the key plays.
        constexpr double unmaskThresholdBelowKeyDb = 6.0;

        double edgeFactor (double q)
        {
            // Half the bandwidth of a bell of this Q, as a frequency ratio.
            const auto octaves = 2.0 / std::log (2.0) * std::asinh (1.0 / (2.0 * q));
            return std::pow (2.0, 0.5 * octaves);
        }
    }

    AssistPanel::AssistPanel (EqModel& m, dsp::EditorLink& l, KeySource k, std::function<double()> sr)
        : model (m), link (l), keys (std::move (k)), sampleRateSource (std::move (sr)),
          fineHz (dsp::curvefit::logFrequencies (20.0, 20000.0, 48)),
          coarseHz (dsp::curvefit::logFrequencies (20.0, 20000.0, 12)),
          scratch ((size_t) dsp::AudioTap::capacity)
    {
        for (auto* tab : { &resonancesTab, &unmaskTab })
        {
            tab->setClickingTogglesState (false);
            tab->setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.35f));
            addAndMakeVisible (*tab);
        }

        resonancesTab.setTooltip ("Find narrow resonances in this track and tame them with dynamic cuts");
        unmaskTab.setTooltip ("Make room for another track: duck this one where they collide, only while the other plays");
        resonancesTab.onClick = [this] { setMode (Mode::resonances); };
        unmaskTab.onClick = [this] { setMode (Mode::unmask); };

        keyBox.setTooltip ("Key: the track that should cut through this one. Pick another FabCutie, "
                           "or the host sidechain. Bands Assist adds here listen to it.");
        keyBox.onChange = [this] { chooseKey(); };
        addChildComponent (keyBox);

        learnButton.setClickingTogglesState (true);
        learnButton.setTooltip ("Play the track (and its key) while this is on");
        learnButton.onClick = [this] { setLearning (learnButton.getToggleState()); };

        resetButton.setTooltip ("Forget what was learned");
        resetButton.onClick = [this] { reset(); };

        applyButton.setTooltip ("Add the suggested bands");
        applyButton.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.35f));
        applyButton.onClick = [this] { apply(); };

        for (auto* b : { &learnButton, &resetButton, &applyButton })
            addAndMakeVisible (*b);

        for (auto* s : { &amount, &bandCount })
        {
            s->setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.3f));
            s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s->setTextBoxIsEditable (false);
            s->onValueChange = [this] { analyse(); repaint(); };
            addAndMakeVisible (*s);
        }

        refreshKeys();
        setMode (Mode::resonances);
    }

    AssistPanel::~AssistPanel()
    {
        link.assistLearning.store (false);
    }

    void AssistPanel::visibilityChanged()
    {
        if (! isVisible())
            setLearning (false);
        else
            refreshKeys();
    }

    double AssistPanel::getSampleRate() const
    {
        const auto sr = sampleRateSource ? sampleRateSource() : 0.0;
        return sr > 0.0 ? sr : 48000.0;
    }

    void AssistPanel::refreshKeys()
    {
        keyBox.clear (juce::dontSendNotification);
        keyIds.clear();

        keyBox.addItem ("Key: host sidechain", 1);
        keyIds.push_back (0);

        const auto current = keys.get ? keys.get() : 0;
        auto selected = 0;
        auto found = current == 0;

        if (keys.choices)
        {
            for (const auto& choice : keys.choices())
            {
                keyIds.push_back (choice.id);
                keyBox.addItem ("Key: " + choice.name, (int) keyIds.size());

                if (choice.id == current)
                {
                    selected = (int) keyIds.size() - 1;
                    found = true;
                }
            }
        }

        // A saved key whose instance is not loaded (yet) stays chosen.
        if (! found)
        {
            keyIds.push_back (current);
            keyBox.addItem ("Key: (instance not found)", (int) keyIds.size());
            selected = (int) keyIds.size() - 1;
        }

        keyBox.setSelectedItemIndex (selected, juce::dontSendNotification);
    }

    void AssistPanel::chooseKey()
    {
        const auto index = keyBox.getSelectedItemIndex();

        if (index >= 0 && index < (int) keyIds.size() && keys.set)
            keys.set (keyIds[(size_t) index]);

        // A different key: what was learned from the old one no longer applies.
        key.reset();
        analyse();
        repaint();
    }

    void AssistPanel::setMode (Mode newMode)
    {
        mode = newMode;
        resonancesTab.setToggleState (mode == Mode::resonances, juce::dontSendNotification);
        unmaskTab.setToggleState (mode == Mode::unmask, juce::dontSendNotification);
        keyBox.setVisible (mode == Mode::unmask);

        if (mode == Mode::resonances)
        {
            amount.setRange (2.0, 12.0, 0.5);
            amount.setValue (6.0, juce::dontSendNotification);
            amount.setDoubleClickReturnValue (true, 6.0);
            amount.textFromValueFunction = [] (double v) { return "Sensitivity " + juce::String (v, 1) + " dB"; };
            amount.setTooltip ("How far a peak must stand out of the spectrum around it");

            bandCount.setRange (1.0, 8.0, 1.0);
            bandCount.setValue (5.0, juce::dontSendNotification);
            bandCount.setDoubleClickReturnValue (true, 5.0);
        }
        else
        {
            amount.setRange (1.0, 12.0, 0.5);
            amount.setValue (4.0, juce::dontSendNotification);
            amount.setDoubleClickReturnValue (true, 4.0);
            amount.textFromValueFunction = [] (double v) { return "Depth " + juce::String (v, 1) + " dB"; };
            amount.setTooltip ("How far this track ducks while the key plays");

            bandCount.setRange (1.0, 6.0, 1.0);
            bandCount.setValue (3.0, juce::dontSendNotification);
            bandCount.setDoubleClickReturnValue (true, 3.0);
        }

        bandCount.textFromValueFunction = [] (double v) { return "Up to " + juce::String (juce::roundToInt (v)) + " bands"; };
        bandCount.setTooltip ("The most bands to add");
        amount.updateText();
        bandCount.updateText();

        analyse();
        resized();
        repaint();
    }

    void AssistPanel::setLearning (bool on)
    {
        learning = on;
        learnButton.setToggleState (on, juce::dontSendNotification);

        // Drop anything the taps held from before, then let the audio
        // thread fill them only while listening.
        link.assistLearning.store (false);
        for (auto* tap : { &link.assistSource, &link.assistKey })
            while (tap->pull (scratch.data(), (int) scratch.size()) > 0) {}

        link.assistLearning.store (on);

        if (on)
            startTimerHz (30);
        else
            stopTimer();

        analyse();
        repaint();
    }

    void AssistPanel::reset()
    {
        source.reset();
        key.reset();
        analyse();
        repaint();
    }

    void AssistPanel::timerCallback()
    {
        for (int n; (n = link.assistSource.pull (scratch.data(), (int) scratch.size())) > 0;)
            source.push (scratch.data(), n);

        for (int n; (n = link.assistKey.pull (scratch.data(), (int) scratch.size())) > 0;)
            key.push (scratch.data(), n);

        // Suggestions only need to move a few times a second.
        static constexpr int analyseEvery = 6;
        if (++timerTicks % analyseEvery == 0)
            analyse();

        repaint();
    }

    void AssistPanel::analyse()
    {
        suggestions.clear();
        sourceDb.clear();
        keyDb.clear();

        const auto sampleRate = getSampleRate();
        const auto maxCount = juce::roundToInt (bandCount.getValue());

        if (mode == Mode::resonances)
        {
            if (source.hasData())
            {
                sourceDb = source.levelsDb (fineHz, sampleRate, 1.0 / 24.0);

                dsp::assist::ResonanceOptions options;
                options.sensitivityDb = amount.getValue();
                options.maxCount = maxCount;
                options.maxHz = std::min (options.maxHz, 0.45 * sampleRate);
                suggestions = dsp::assist::findResonances (fineHz, sourceDb, source.levelsDb (fineHz, sampleRate, 1.0), options);
            }
        }
        else
        {
            if (source.hasData())
                sourceDb = source.levelsDb (coarseHz, sampleRate);

            if (key.hasData())
                keyDb = key.levelsDb (coarseHz, sampleRate);

            if (source.hasData() && key.hasData())
            {
                dsp::assist::CollisionOptions options;
                options.maxCount = maxCount;
                suggestions = dsp::assist::findCollisions (coarseHz, sourceDb, keyDb, options);
            }
        }

        // Spots that already have a dynamic band of this kind are done.
        const auto detector = mode == Mode::unmask ? dsp::DetectorSource::external : dsp::DetectorSource::internal;
        const auto bands = model.getAllBands();

        suggestions.erase (std::remove_if (suggestions.begin(), suggestions.end(), [&] (const dsp::assist::Suggestion& s)
        {
            return std::any_of (bands.begin(), bands.end(), [&] (const dsp::BandSettings& b)
            {
                return b.enabled && b.dynamics.enabled && b.dynamics.source == detector
                    && std::abs (std::log2 (b.frequency / s.frequency)) < 1.0 / 6.0;
            });
        }), suggestions.end());

        applyButton.setEnabled (! suggestions.empty() && model.findFreeBand() >= 0);
    }

    void AssistPanel::apply()
    {
        analyse();

        using BandParam = params::BandParam;
        const auto sampleRate = getSampleRate();
        const auto unmask = mode == Mode::unmask;

        for (const auto& s : suggestions)
        {
            const auto band = model.findFreeBand();
            if (band < 0)
                break;

            // The detector hears the band's own region of its input (or of
            // the key), so the threshold starts from that region's level.
            const auto edge = edgeFactor (s.q);
            const auto& heard = unmask ? key : source;
            auto threshold = heard.rangeLevelDb (s.frequency / edge, s.frequency * edge, sampleRate);

            if (unmask)
                threshold -= unmaskThresholdBelowKeyDb;

            const auto depth = unmask ? (float) amount.getValue()
                                      : std::min ((float) s.amountDb, maxResonanceCutDb);

            model.addBand (band, dsp::FilterType::bell, (float) s.frequency, 0.0f);
            model.set (band, BandParam::q, (float) s.q);
            model.set (band, BandParam::threshold, juce::jlimit (params::range::thresholdMinDb, params::range::thresholdMaxDb, (float) threshold));
            model.set (band, BandParam::range, -depth);
            model.set (band, BandParam::attack, unmask ? unmaskAttackMs : resonanceAttackMs);
            model.set (band, BandParam::release, unmask ? unmaskReleaseMs : resonanceReleaseMs);
            model.set (band, BandParam::detectorSource, (float) (unmask ? dsp::DetectorSource::external : dsp::DetectorSource::internal));
            model.set (band, BandParam::detectorFilter, (float) dsp::DetectorFilter::band);
            model.set (band, BandParam::spectral, 0.0f);
            model.set (band, BandParam::dynamic, 1.0f);
        }

        analyse();
        repaint();
    }

    //==========================================================================
    void AssistPanel::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (colours::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (colours::panelOutline);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
        g.setColour (colours::accent);
        g.fillRoundedRectangle (bounds.withHeight (3.0f).reduced (10.0f, 0.0f), 1.5f);

        auto header = getLocalBounds().reduced (12, 8).removeFromTop (22);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("Assist", header, juce::Justification::centredLeft);

        if (mode == Mode::resonances)
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (11.5f));
            g.drawFittedText ("Finds narrow peaks that ring out and adds a dynamic cut on each.",
                              keyBox.getBounds(), juce::Justification::centredLeft, 2);
        }

        // Preview: the learned spectra (each against its own loudest point)
        // and a marker on every suggestion, 20 Hz to 20 kHz.
        const auto area = previewArea.toFloat();
        g.setColour (colours::graphBottom);
        g.fillRoundedRectangle (area, 4.0f);

        const auto xFor = [&] (double f) { return area.getX() + area.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };

        g.setColour (colours::gridMinor);
        for (auto f : { 100.0, 1000.0, 10000.0 })
            g.fillRect (juce::Rectangle<float> (xFor (f), area.getY(), 1.0f, area.getHeight()));

        const auto& hz = mode == Mode::resonances ? fineHz : coarseHz;

        auto drawTrace = [&] (const std::vector<double>& db, juce::Colour colour)
        {
            if (db.size() != hz.size())
                return;

            const auto top = *std::max_element (db.begin(), db.end());
            const auto yFor = [&] (double v)
            {
                const auto rel = juce::jlimit (0.0, 1.0, (top - v) / previewRangeDb);
                return area.getY() + 4.0f + (area.getHeight() - 8.0f) * (float) rel;
            };

            juce::Path path;
            for (size_t i = 0; i < hz.size(); ++i)
            {
                const juce::Point<float> p (xFor (hz[i]), yFor (db[i]));
                if (i == 0) path.startNewSubPath (p);
                else        path.lineTo (p);
            }

            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (1.3f));
        };

        drawTrace (keyDb, colours::spectrumExternal.withAlpha (0.85f));
        drawTrace (sourceDb, colours::spectrumPre);

        for (const auto& s : suggestions)
        {
            const auto x = xFor (s.frequency);
            g.setColour ((mode == Mode::unmask ? colours::collision : colours::accent).withAlpha (0.25f));
            const auto halfWidth = juce::jmax (2.0f, (xFor (s.frequency * edgeFactor (s.q)) - x));
            g.fillRect (juce::Rectangle<float> (x - halfWidth, area.getY(), 2.0f * halfWidth, area.getHeight()));
            g.setColour (mode == Mode::unmask ? colours::collision : colours::accent);
            g.fillRect (juce::Rectangle<float> (x - 0.75f, area.getY(), 1.5f, area.getHeight()));
        }

        // What to do next.
        const auto sampleRate = getSampleRate();
        const auto found = juce::String ((int) suggestions.size());
        juce::String status;

        if (learning)
        {
            status = "Learning... " + juce::String (source.getSeconds (sampleRate), 1) + " s";

            if (mode == Mode::unmask && ! link.sidechainConnected.load())
                status << "  (no key heard)";
            else if (! suggestions.empty())
                status << ", " << found << " found";
        }
        else if (! source.hasData())
            status = mode == Mode::unmask ? "Pick a key, play both tracks, press Learn" : "Play the track and press Learn";
        else if (mode == Mode::unmask && ! key.hasData())
            status = "No key heard: pick a key and Learn again";
        else if (suggestions.empty())
            status = mode == Mode::unmask ? "No collisions left to fix" : "No resonances left at this sensitivity";
        else if (model.findFreeBand() < 0)
            status = "No free bands left";
        else
            status = found + (mode == Mode::unmask ? (suggestions.size() == 1 ? " collision" : " collisions")
                                                   : (suggestions.size() == 1 ? " resonance" : " resonances"))
                   + " ready to apply";

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (status, statusArea, juce::Justification::centredLeft, true);
    }

    void AssistPanel::resized()
    {
        auto area = getLocalBounds().reduced (12, 8);
        constexpr int rowHeight = 24, gap = 6;

        auto header = area.removeFromTop (22);
        unmaskTab.setBounds (header.removeFromRight (70).withSizeKeepingCentre (70, 22));
        header.removeFromRight (4);
        resonancesTab.setBounds (header.removeFromRight (90).withSizeKeepingCentre (90, 22));
        area.removeFromTop (gap);

        keyBox.setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (gap);

        auto row = area.removeFromTop (rowHeight);
        resetButton.setBounds (row.removeFromRight (80));
        row.removeFromRight (gap);
        learnButton.setBounds (row);
        area.removeFromTop (gap);

        row = area.removeFromTop (rowHeight);
        amount.setBounds (row.removeFromLeft ((row.getWidth() - gap) / 2));
        row.removeFromLeft (gap);
        bandCount.setBounds (row);
        area.removeFromTop (gap);

        auto bottom = area.removeFromBottom (rowHeight);
        applyButton.setBounds (bottom.removeFromRight (80));
        bottom.removeFromRight (gap);
        statusArea = bottom;

        area.removeFromBottom (gap);
        previewArea = area;
    }
}
