#include "MatchPanel.h"
#include "Theme.h"
#include "dsp/CurveFit.h"

namespace fabcutie::ui
{
    namespace
    {
        constexpr double maxMatchDb = 18.0;
        constexpr float previewRangeDb = 12.0f;

        // The captured reference is saved with the session, next to the
        // parameters, as "v1" followed by one level per match frequency.
        const juce::Identifier captureId { "matchReference" };
    }

    MatchPanel::MatchPanel (EqModel& m, dsp::EditorLink& l, std::function<double()> sr)
        : model (m), link (l), sampleRateSource (std::move (sr)),
          hz (dsp::curvefit::logFrequencies (20.0, 20000.0, 12)),
          scratch ((size_t) dsp::AudioTap::capacity)
    {
        referenceBox.addItemList ({ "Sidechain", "Captured" }, 1);
        referenceBox.setSelectedItemIndex (0, juce::dontSendNotification);
        referenceBox.setTooltip ("Reference: the sidechain input, or a reference captured earlier");
        referenceBox.onChange = [this] { updateCurve(); repaint(); };
        addAndMakeVisible (referenceBox);

        captureButton.setClickingTogglesState (true);
        captureButton.setTooltip ("Play the reference through the plugin while this is on");
        captureButton.onClick = [this] { setListening (captureButton.getToggleState() ? Listening::capture : Listening::nothing); };

        learnButton.setClickingTogglesState (true);
        learnButton.setTooltip ("Play your track (and the sidechain reference) while this is on");
        learnButton.onClick = [this] { setListening (learnButton.getToggleState() ? Listening::learn : Listening::nothing); };

        resetButton.setTooltip ("Forget what was learned from the input");
        resetButton.onClick = [this] { reset(); };

        applyButton.setTooltip ("Add bands that apply the match curve");
        applyButton.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.35f));
        applyButton.onClick = [this] { apply(); };

        for (auto* b : { &captureButton, &learnButton, &resetButton, &applyButton })
            addAndMakeVisible (*b);

        amount.setRange (0.0, 100.0, 1.0);
        amount.setValue (100.0, juce::dontSendNotification);
        amount.setDoubleClickReturnValue (true, 100.0);
        amount.textFromValueFunction = [] (double v) { return "Amount " + juce::String (juce::roundToInt (v)) + "%"; };
        amount.onValueChange = [this] { updateCurve(); repaint(); };

        bandCount.setRange (1.0, 12.0, 1.0);
        bandCount.setValue (8.0, juce::dontSendNotification);
        bandCount.setDoubleClickReturnValue (true, 8.0);
        bandCount.textFromValueFunction = [] (double v) { return "Up to " + juce::String (juce::roundToInt (v)) + " bands"; };

        for (auto* s : { &amount, &bandCount })
        {
            s->setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.3f));
            s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s->setTextBoxIsEditable (false);
            s->updateText();
            addAndMakeVisible (*s);
        }

        loadCapture();

        if (! capturedDb.empty())
            referenceBox.setSelectedItemIndex (1, juce::dontSendNotification);
    }

    MatchPanel::~MatchPanel()
    {
        link.matchLearning.store (false);
    }

    void MatchPanel::visibilityChanged()
    {
        // Closing the panel stops listening.
        if (! isVisible())
            setListening (Listening::nothing);
    }

    double MatchPanel::getSampleRate() const
    {
        const auto sr = sampleRateSource ? sampleRateSource() : 0.0;
        return sr > 0.0 ? sr : 48000.0;
    }

    bool MatchPanel::hasReference() const noexcept
    {
        return usesCapture() ? ! capturedDb.empty() : sidechain.hasData();
    }

    void MatchPanel::setListening (Listening newListening)
    {
        if (listening == Listening::capture && newListening != Listening::capture && capture.hasData())
        {
            capturedDb = capture.levelsDb (hz, getSampleRate());
            saveCapture();
            referenceBox.setSelectedItemIndex (1, juce::dontSendNotification);
        }

        listening = newListening;

        if (listening == Listening::capture)
            capture.reset();

        captureButton.setToggleState (listening == Listening::capture, juce::dontSendNotification);
        learnButton.setToggleState (listening == Listening::learn, juce::dontSendNotification);

        // Drop anything the taps held from before, then let the audio
        // thread fill them only while listening.
        link.matchLearning.store (false);
        for (auto* tap : { &link.matchSource, &link.matchReference })
            while (tap->pull (scratch.data(), (int) scratch.size()) > 0) {}

        link.matchLearning.store (listening != Listening::nothing);

        if (listening != Listening::nothing)
            startTimerHz (30);
        else
            stopTimer();

        updateCurve();
        repaint();
    }

    void MatchPanel::reset()
    {
        source.reset();
        sidechain.reset();
        updateCurve();
        repaint();
    }

    void MatchPanel::timerCallback()
    {
        for (int n; (n = link.matchSource.pull (scratch.data(), (int) scratch.size())) > 0;)
            (listening == Listening::capture ? capture : source).push (scratch.data(), n);

        for (int n; (n = link.matchReference.pull (scratch.data(), (int) scratch.size())) > 0;)
            if (listening == Listening::learn)
                sidechain.push (scratch.data(), n);

        // The curve only needs to move a few times a second.
        static constexpr int curveEvery = 6;
        if (++timerTicks % curveEvery == 0)
            updateCurve();

        repaint();
    }

    void MatchPanel::updateCurve()
    {
        curveValid = source.hasData() && hasReference();

        if (! curveValid)
        {
            curve.assign (hz.size(), 0.0);
            applyButton.setEnabled (false);
            return;
        }

        const auto sampleRate = getSampleRate();
        const auto reference = usesCapture() ? capturedDb : sidechain.levelsDb (hz, sampleRate);
        curve = dsp::match::curve (hz, source.levelsDb (hz, sampleRate), reference, amount.getValue() / 100.0, maxMatchDb);
        applyButton.setEnabled (model.findFreeBand() >= 0);
    }

    void MatchPanel::apply()
    {
        updateCurve();
        if (! curveValid)
            return;

        // The input is learned before the EQ, so the curve is what the
        // whole EQ should do; bands already on count towards it.
        model.addBandsForCurve (hz, curve, juce::roundToInt (bandCount.getValue()), getSampleRate());

        updateCurve();
        repaint();
    }

    void MatchPanel::loadCapture()
    {
        const auto text = model.getState().state.getProperty (captureId).toString();
        auto tokens = juce::StringArray::fromTokens (text, " ", "");

        if (tokens.size() != (int) hz.size() + 1 || tokens[0] != "v1")
            return;

        capturedDb.clear();
        for (int i = 1; i < tokens.size(); ++i)
            capturedDb.push_back (tokens[i].getDoubleValue());
    }

    void MatchPanel::saveCapture()
    {
        juce::String text ("v1");
        for (auto db : capturedDb)
            text << " " << juce::String (db, 2);

        model.getState().state.setProperty (captureId, text, nullptr);
    }

    //==========================================================================
    void MatchPanel::paint (juce::Graphics& g)
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
        g.drawText ("EQ Match", header, juce::Justification::centredLeft);

        // Preview: the match curve over +/- 12 dB, 20 Hz to 20 kHz.
        const auto area = previewArea.toFloat();
        g.setColour (colours::graphBottom);
        g.fillRoundedRectangle (area, 4.0f);

        const auto xFor = [&] (double f) { return area.getX() + area.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
        const auto yFor = [&] (double db) { return area.getCentreY() - area.getHeight() * 0.5f * (float) juce::jlimit (-1.0, 1.0, db / previewRangeDb); };

        g.setColour (colours::gridMinor);
        for (auto f : { 100.0, 1000.0, 10000.0 })
            g.fillRect (juce::Rectangle<float> (xFor (f), area.getY(), 1.0f, area.getHeight()));

        g.setColour (colours::gridMajor);
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getCentreY(), area.getWidth(), 1.0f));

        if (curveValid)
        {
            juce::Path path;
            for (size_t i = 0; i < hz.size(); ++i)
            {
                const juce::Point<float> p (xFor (hz[i]), yFor (curve[i]));
                if (i == 0) path.startNewSubPath (p);
                else        path.lineTo (p);
            }

            g.setColour (colours::accent);
            g.strokePath (path, juce::PathStrokeType (1.8f));
        }

        // What to do next.
        const auto sampleRate = getSampleRate();
        juce::String status;

        if (listening == Listening::capture)
            status = "Capturing reference... " + juce::String (capture.getSeconds (sampleRate), 1) + " s";
        else if (listening == Listening::learn)
            status = "Learning... " + juce::String (source.getSeconds (sampleRate), 1) + " s"
                   + (! usesCapture() && ! link.sidechainConnected.load() ? "  (no sidechain)" : "");
        else if (usesCapture() && capturedDb.empty())
            status = "Play the reference and press Capture";
        else if (! usesCapture() && ! sidechain.hasData() && source.hasData())
            status = "No sidechain heard: route the reference to it";
        else if (! source.hasData())
            status = "Play your track and press Learn";
        else if (model.findFreeBand() < 0)
            status = "No free bands left";
        else
            status = "Ready to apply";

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (status, statusArea, juce::Justification::centredLeft, true);
    }

    void MatchPanel::resized()
    {
        auto area = getLocalBounds().reduced (12, 8);
        area.removeFromTop (28);

        constexpr int rowHeight = 24, gap = 6;

        auto row = area.removeFromTop (rowHeight);
        captureButton.setBounds (row.removeFromRight (80));
        row.removeFromRight (gap);
        referenceBox.setBounds (row);
        area.removeFromTop (gap);

        row = area.removeFromTop (rowHeight);
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
