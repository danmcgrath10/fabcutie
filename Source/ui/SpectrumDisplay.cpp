#include "SpectrumDisplay.h"
#include "Theme.h"

namespace fabcutie::ui
{
    namespace
    {
        constexpr float pointSpacing = 2.0f;
        constexpr float topDb = 0.0f; // level at the top of the graph
        constexpr float tiltPivotHz = 1000.0f;

        juce::String frequencyText (float hz)
        {
            if (hz >= 1000.0f)
                return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";

            return juce::String (juce::roundToInt (hz)) + " Hz";
        }

        // The nearest note, e.g. "A4", so a peak can be matched to the music.
        juce::String noteName (float hz)
        {
            const auto midi = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
            return juce::MidiMessage::getMidiNoteName (midi, true, true, 4);
        }
    }

    SpectrumDisplay::SpectrumDisplay (EqGraph& g, dsp::EditorLink& l, std::function<double()> sr)
        : graph (g), link (l), sampleRateSource (std::move (sr)),
          pre (l.pre, colours::spectrumPre),
          post (l.post, colours::spectrumPost),
          external (l.external, colours::spectrumExternal),
          scratch ((size_t) dsp::AudioTap::capacity)
    {
        setInterceptsMouseClicks (false, false);
        setSettings (settings);
        startTimerHz (50);
    }

    SpectrumDisplay::~SpectrumDisplay()
    {
        link.analyzerActive.store (false);
    }

    void SpectrumDisplay::setSettings (const AnalyzerSettings& newSettings)
    {
        const auto wasFrozen = settings.freeze;
        settings = newSettings;

        for (auto* trace : { &pre, &post, &external })
            trace->analyzer.setOrder (settings.getFftOrder());

        link.analyzerActive.store (settings.anySpectrum());

        if (wasFrozen && ! settings.freeze)
            resetClocks();

        rebuildPoints();
        repaint();
    }

    void SpectrumDisplay::resetClocks()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();

        for (auto* trace : { &pre, &post, &external })
            trace->lastUpdate = now;
    }

    //==========================================================================
    void SpectrumDisplay::resized()
    {
        rebuildPoints();
    }

    void SpectrumDisplay::rebuildPoints()
    {
        lastGeometry = graph.getGeometry();
        plot = graph.getPlotArea();

        const auto numPoints = juce::jmax (2, (int) (lastGeometry.width / pointSpacing) + 1);
        pointX.resize ((size_t) numPoints);
        pointLowHz.resize ((size_t) numPoints);
        pointHighHz.resize ((size_t) numPoints);

        for (size_t i = 0; i < (size_t) numPoints; ++i)
        {
            const auto x = lastGeometry.x + pointSpacing * (float) i;
            pointX[i] = x;
            pointLowHz[i] = lastGeometry.frequencyForX (x - pointSpacing * 0.5f);
            pointHighHz[i] = lastGeometry.frequencyForX (x + pointSpacing * 0.5f);
        }

        for (auto* trace : { &pre, &post, &external })
            updateTrace (*trace);
    }

    void SpectrumDisplay::timerCallback()
    {
        const auto geometry = graph.getGeometry();
        if (! juce::exactlyEqual (geometry.x, lastGeometry.x) || ! juce::exactlyEqual (geometry.width, lastGeometry.width)
            || graph.getPlotArea() != plot)
            rebuildPoints();

        const auto now = juce::Time::getMillisecondCounterHiRes();

        // While frozen, or while a peak is offered for grabbing, the display
        // holds still. The taps are drained either way so they stay current.
        const auto hold = settings.freeze || highlight.has_value();

        auto sampleRate = sampleRateSource ? sampleRateSource() : 0.0;
        if (sampleRate <= 0.0) sampleRate = 48000.0;

        pre.visible = settings.showPre;
        post.visible = settings.showPost;
        external.visible = settings.showExternal && link.sidechainConnected.load();

        auto changed = false;

        for (auto* trace : { &pre, &post, &external })
        {
            for (int n; (n = trace->tap.pull (scratch.data(), (int) scratch.size())) > 0;)
                if (! hold && trace->visible)
                    trace->analyzer.push (scratch.data(), n);

            if (hold || ! trace->visible)
                continue;

            if (! juce::exactlyEqual (trace->analyzer.getSampleRate(), sampleRate))
            {
                trace->analyzer.setSampleRate (sampleRate);
                trace->analyzer.reset();
            }

            if (trace->analyzer.update ((now - trace->lastUpdate) * 0.001, settings.getReleaseSeconds()))
            {
                trace->lastUpdate = now;
                updateTrace (*trace);
                changed = true;
            }
        }

        if (changed)
            repaint();
    }

    void SpectrumDisplay::updateTrace (Trace& trace)
    {
        const auto tilt = settings.getTiltDbPerOct();
        const auto nyquist = (float) trace.analyzer.getSampleRate() * 0.5f;

        trace.db.resize (pointX.size());

        for (size_t i = 0; i < pointX.size(); ++i)
        {
            const auto lowHz = pointLowHz[i], highHz = pointHighHz[i];

            if (lowHz >= nyquist)
            {
                trace.db[i] = dsp::SpectrumAnalyzer::floorDb;
                continue;
            }

            const auto centreHz = std::sqrt (lowHz * highHz);
            trace.db[i] = trace.analyzer.levelForSpan (lowHz, std::min (highHz, nyquist))
                        + tilt * std::log2 (centreHz / tiltPivotHz);
        }
    }

    float SpectrumDisplay::yForLevel (float db) const noexcept
    {
        const auto range = settings.getRangeDb();
        const auto clamped = juce::jlimit (topDb - range - 6.0f, topDb + 6.0f, db);
        return plot.getY() + (topDb - clamped) / range * plot.getHeight();
    }

    //==========================================================================
    juce::Path SpectrumDisplay::tracePath (const Trace& trace, bool closed) const
    {
        juce::Path path;

        if (trace.db.size() != pointX.size() || pointX.empty())
            return path;

        const auto bottom = plot.getBottom() + 2.0f;

        if (closed)
            path.startNewSubPath (pointX.front(), bottom);

        for (size_t i = 0; i < pointX.size(); ++i)
        {
            const auto y = std::min (bottom, yForLevel (trace.db[i]));

            if (i == 0 && ! closed) path.startNewSubPath (pointX[i], y);
            else                    path.lineTo (pointX[i], y);
        }

        if (closed)
        {
            path.lineTo (pointX.back(), bottom);
            path.closeSubPath();
        }

        return path;
    }

    void SpectrumDisplay::paint (juce::Graphics& g)
    {
        if (! settings.anySpectrum())
            return;

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (plot.toNearestInt());

        if (pre.visible)
        {
            g.setColour (pre.colour.withAlpha (0.10f));
            g.fillPath (tracePath (pre, true));
            g.setColour (pre.colour.withAlpha (0.45f));
            g.strokePath (tracePath (pre, false), juce::PathStrokeType (1.0f));
        }

        if (external.visible)
        {
            g.setColour (external.colour.withAlpha (0.07f));
            g.fillPath (tracePath (external, true));
            g.setColour (external.colour.withAlpha (0.8f));
            g.strokePath (tracePath (external, false), juce::PathStrokeType (1.2f));
        }

        if (post.visible)
        {
            g.setGradientFill (juce::ColourGradient (post.colour.withAlpha (0.34f), 0.0f, plot.getY(),
                                                     post.colour.withAlpha (0.04f), 0.0f, plot.getBottom(), false));
            g.fillPath (tracePath (post, true));
            g.setColour (post.colour.withAlpha (0.75f));
            g.strokePath (tracePath (post, false), juce::PathStrokeType (1.2f));
        }

        if (settings.collisions && post.visible && external.visible)
            drawCollisions (g);

        if (settings.freeze)
        {
            g.setColour (colours::textDim.withAlpha (0.5f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText ("FROZEN", plot.withTrimmedLeft (12.0f).withTrimmedBottom (8.0f), juce::Justification::bottomLeft);
        }

        drawHighlight (g);
    }

    void SpectrumDisplay::drawCollisions (juce::Graphics& g)
    {
        // Two signals mask each other where both are loud and close in level.
        // Shade those frequencies red, stronger the more they overlap.
        const auto range = settings.getRangeDb();
        const auto threshold = topDb - range * 0.55f;

        const auto size = std::min ({ pointX.size(), post.db.size(), external.db.size() });

        for (size_t i = 0; i < size; ++i)
        {
            const auto a = post.db[i], b = external.db[i];
            const auto loudness = juce::jlimit (0.0f, 1.0f, (std::min (a, b) - threshold) / (range * 0.25f));
            const auto closeness = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (a - b) / 20.0f);
            const auto strength = loudness * closeness;

            if (strength < 0.02f)
                continue;

            const auto top = yForLevel (std::max (a, b));
            const auto column = juce::Rectangle<float> (pointX[i] - pointSpacing * 0.5f, top,
                                                        pointSpacing, plot.getBottom() - top);

            g.setColour (colours::collision.withAlpha (0.32f * strength));
            g.fillRect (column);

            // A heat strip along the top makes clashes easy to spot at a glance.
            g.setColour (colours::collision.withAlpha (0.85f * strength));
            g.fillRect (column.withY (plot.getY()).withHeight (3.0f));
        }
    }

    void SpectrumDisplay::drawHighlight (juce::Graphics& g)
    {
        if (! highlight.has_value())
            return;

        const auto p = *highlight;
        const auto hz = lastGeometry.frequencyForX (p.x);

        g.setColour (colours::text.withAlpha (0.25f));
        g.drawVerticalLine (juce::roundToInt (p.x), p.y, plot.getBottom());

        g.setColour (colours::text);
        g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (p), 1.5f);

        const auto text = frequencyText (hz) + "  " + noteName (hz);
        const juce::Font font (juce::FontOptions (11.5f));
        const auto width = (float) juce::GlyphArrangement::getStringWidthInt (font, text) + 14.0f;
        auto box = juce::Rectangle<float> (width, 20.0f).withCentre ({ p.x, p.y - 20.0f });
        box = box.constrainedWithin (plot);

        g.setColour (juce::Colour (0xe0101217));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (colours::text);
        g.setFont (font);
        g.drawText (text, box, juce::Justification::centred);
    }

    //==========================================================================
    std::optional<juce::Point<float>> SpectrumDisplay::findPeakNear (juce::Point<float> mouse)
    {
        if (! settings.spectrumGrab)
            return std::nullopt;

        const auto* trace = post.visible ? &post : pre.visible ? &pre : nullptr;

        if (trace == nullptr || trace->db.size() != pointX.size() || pointX.empty())
            return std::nullopt;

        // The loudest point within a short reach of the mouse.
        constexpr float reach = 16.0f;
        const auto first = (size_t) juce::jlimit (0, (int) pointX.size() - 1, (int) ((mouse.x - reach - lastGeometry.x) / pointSpacing));
        const auto last = (size_t) juce::jlimit (0, (int) pointX.size() - 1, (int) ((mouse.x + reach - lastGeometry.x) / pointSpacing));

        auto best = first;
        for (auto i = first; i <= last; ++i)
            if (trace->db[i] > trace->db[best])
                best = i;

        // It must be a real peak (not just the edge of a slope), stand out of
        // the noise floor, and be close to the mouse vertically too.
        const auto isLocalMax = best > 0 && best + 1 < pointX.size()
                             && trace->db[best] >= trace->db[best - 1] && trace->db[best] >= trace->db[best + 1];
        const auto y = yForLevel (trace->db[best]);

        if (! isLocalMax || y > plot.getBottom() - plot.getHeight() * 0.08f || std::abs (mouse.y - y) > 40.0f)
            return std::nullopt;

        return juce::Point<float> (pointX[best], y);
    }

    void SpectrumDisplay::setHighlightedPeak (std::optional<juce::Point<float>> peak)
    {
        if (highlight == peak)
            return;

        // Coming out of a hold, do not treat the pause as elapsed time.
        if (highlight.has_value() && ! peak.has_value())
            resetClocks();

        highlight = peak;
        repaint();
    }
}
