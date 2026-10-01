#include "EqGraph.h"
#include "BandClipboard.h"
#include "Theme.h"
#include "dsp/FilterDesign.h"
#include "dsp/Notes.h"

namespace fabcutie::ui
{
    namespace
    {
        constexpr float nodeRadius = 8.0f;
        constexpr float hitRadius  = 11.0f;
        constexpr float maxGainDb  = params::range::bandGainMaxDb;

        bool sameSettings (const dsp::BandSettings& a, const dsp::BandSettings& b) noexcept
        {
            return a.sameStructure (b) && juce::exactlyEqual (a.frequency, b.frequency)
                && juce::exactlyEqual (a.gainDb, b.gainDb) && juce::exactlyEqual (a.q, b.q);
        }

        juce::String frequencyLabel (float hz)
        {
            return hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        }

        const char* placementLetter (int placement)
        {
            switch ((dsp::Placement) placement)
            {
                case dsp::Placement::left:  return "L";
                case dsp::Placement::right: return "R";
                case dsp::Placement::mid:   return "M";
                case dsp::Placement::side:  return "S";
                case dsp::Placement::stereo: break;
            }

            return "";
        }
    }

    EqGraph::EqGraph (EqModel& m, std::function<double()> sr)
        : model (m), sampleRateSource (std::move (sr))
    {
        setWantsKeyboardFocus (true);
        setOpaque (true);
        refresh (true);
        startTimerHz (60);
    }

    EqGraph::~EqGraph()
    {
        if (dragMode == DragMode::nodes)
            endNodeDrag();
    }

    void EqGraph::setBackgroundLayer (juce::Component* layer)
    {
        if (backgroundLayer != nullptr)
            removeChildComponent (backgroundLayer);

        backgroundLayer = layer;

        if (backgroundLayer != nullptr)
        {
            backgroundLayer->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (backgroundLayer, 0);
            backgroundLayer->setBounds (getLocalBounds());
        }
    }

    void EqGraph::setRangeDb (float rangeDb)
    {
        if (juce::exactlyEqual (geometry.rangeDb, rangeDb))
            return;

        geometry.rangeDb = rangeDb;
        repaint();

        if (onBandsChanged) onBandsChanged(); // node positions moved
        if (onRangeChanged) onRangeChanged (rangeDb);
    }

    void EqGraph::selectOnly (int band)
    {
        juce::Array<int> bandsToSelect;
        if (band >= 0) bandsToSelect.add (band);
        setSelection (bandsToSelect, band);
    }

    juce::Point<float> EqGraph::getNodePosition (int band) const
    {
        const auto& s = bands[(size_t) band];
        return { geometry.xForFrequency (s.frequency),
                 juce::jlimit (plot.getY(), plot.getBottom(), geometry.yForDb (nodeDb (s))) };
    }

    //==========================================================================
    void EqGraph::resized()
    {
        const auto bounds = getLocalBounds().toFloat();

        // Keep the +/- range lines a little inside the edges, and leave a strip
        // at the bottom for the frequency labels.
        plot = bounds.withTrimmedTop (14.0f).withTrimmedBottom (22.0f);
        geometry.x = bounds.getX();
        geometry.width = bounds.getWidth();
        geometry.y = plot.getY();
        geometry.height = plot.getHeight();

        rangeButton = { bounds.getRight() - 64.0f, bounds.getY() + 6.0f, 56.0f, 20.0f };

        if (backgroundLayer != nullptr)
            backgroundLayer->setBounds (getLocalBounds());

        curvesValid = false;
        refresh (true);
    }

    void EqGraph::timerCallback()
    {
        refresh (false);

        if (const auto solo = getSoloBand(); solo != lastSolo)
        {
            lastSolo = solo;
            repaint();
        }
    }

    int EqGraph::getSoloBand() const noexcept
    {
        return soloTarget != nullptr ? soloTarget->load() : -1;
    }

    void EqGraph::setSoloBand (int band)
    {
        if (soloTarget != nullptr)
            soloTarget->store (band);

        lastSolo = band;
        repaint();
    }

    void EqGraph::updateGrabPeak (std::optional<juce::Point<float>> mouse)
    {
        std::optional<juce::Point<float>> peak;

        if (mouse.has_value() && peakSource != nullptr && plot.contains (*mouse) && model.findFreeBand() >= 0)
            peak = peakSource->findPeakNear (*mouse);

        if (peak == grabPeak)
            return;

        grabPeak = peak;

        if (peakSource != nullptr)
            peakSource->setHighlightedPeak (grabPeak);
    }

    void EqGraph::refresh (bool force)
    {
        const auto latest = model.getAllBands();
        auto sampleRate = sampleRateSource ? sampleRateSource() : 0.0;
        if (sampleRate <= 0.0) sampleRate = 48000.0;

        if (model.isPianoRollOn() != pianoRollShown)
        {
            pianoRollShown = ! pianoRollShown;
            repaint();
        }

        const auto scale = model.getGainScale();
        bool changed = force || ! curvesValid || ! juce::exactlyEqual (sampleRate, curveSampleRate)
                    || ! juce::exactlyEqual (scale, gainScale);

        for (int b = 0; b < dsp::maxBands && ! changed; ++b)
            changed = ! sameSettings (latest[(size_t) b], bands[(size_t) b]);

        if (! changed)
            return;

        bands = latest;
        curveSampleRate = sampleRate;
        gainScale = scale;
        recomputeCurves();

        // Bands switched off elsewhere (automation, a preset) leave the selection.
        juce::Array<int> stillOn;
        for (auto b : selection)
            if (bands[(size_t) b].enabled)
                stillOn.add (b);

        if (stillOn != selection)
            setSelection (stillOn, stillOn.contains (primary) ? primary : stillOn.isEmpty() ? -1 : stillOn.getLast());

        if (hovered >= 0 && ! bands[(size_t) hovered].enabled)
            hovered = -1;

        if (const auto solo = getSoloBand(); solo >= 0 && ! bands[(size_t) solo].enabled)
            setSoloBand (-1);

        repaint();

        if (onBandsChanged) onBandsChanged();
    }

    void EqGraph::recomputeCurves()
    {
        const auto numPoints = juce::jmax (64, getWidth() / 2 + 1);

        pointX.resize ((size_t) numPoints);
        pointHz.resize ((size_t) numPoints);

        for (int i = 0; i < numPoints; ++i)
        {
            const auto px = geometry.x + geometry.width * (float) i / (float) (numPoints - 1);
            pointX[(size_t) i] = px;
            pointHz[(size_t) i] = geometry.frequencyForX (px);
        }

        for (auto& curve : placementDb)
            curve.assign ((size_t) numPoints, 0.0f);

        placementUsed.fill (false);

        for (int b = 0; b < dsp::maxBands; ++b)
        {
            auto& curve = bandDb[(size_t) b];
            const auto& s = bands[(size_t) b];

            if (! s.enabled)
            {
                curve.clear();
                continue;
            }

            auto scaled = s;
            params::applyGainScale (scaled, gainScale);

            const auto design = dsp::designBand (scaled, curveSampleRate);
            curve.resize ((size_t) numPoints);

            for (size_t i = 0; i < (size_t) numPoints; ++i)
            {
                const auto mag = std::abs (dsp::designResponse (design, pointHz[i], curveSampleRate));
                curve[i] = (float) (20.0 * std::log10 (std::max (mag, 1.0e-12)));
            }

            const auto placement = (size_t) s.placement;
            placementUsed[placement] = true;

            // Stereo bands act on every channel, so they feed every placement curve.
            for (size_t p = 0; p < placementDb.size(); ++p)
                if (p == placement || s.placement == dsp::Placement::stereo)
                    for (size_t i = 0; i < (size_t) numPoints; ++i)
                        placementDb[p][i] += curve[i];
        }

        curvesValid = true;
    }

    //==========================================================================
    float EqGraph::nodeDb (const dsp::BandSettings& s) const noexcept
    {
        return EqModel::usesGain (s.type) ? s.gainDb * gainScale : 0.0f;
    }

    int EqGraph::nodeAt (juce::Point<float> pos) const
    {
        int best = -1;
        auto bestDistance = hitRadius;

        // Later bands draw on top, so prefer them on ties.
        for (int b = dsp::maxBands; --b >= 0;)
        {
            if (! bands[(size_t) b].enabled)
                continue;

            const auto distance = getNodePosition (b).getDistanceFrom (pos);

            if (distance < bestDistance)
            {
                best = b;
                bestDistance = distance;
            }
        }

        return best;
    }

    juce::Array<int> EqGraph::nodesIn (juce::Rectangle<float> area) const
    {
        juce::Array<int> found;

        for (int b = 0; b < dsp::maxBands; ++b)
            if (bands[(size_t) b].enabled && area.contains (getNodePosition (b)))
                found.add (b);

        return found;
    }

    void EqGraph::setSelection (const juce::Array<int>& newSelection, int newPrimary)
    {
        const auto primaryChanged = newPrimary != primary;

        selection = newSelection;
        primary = newPrimary;
        repaint();

        if (primaryChanged && onSelectionChanged)
            onSelectionChanged();
    }

    juce::Array<int> EqGraph::bandsToEdit (int band) const
    {
        if (isSelected (band))
            return selection;

        return { band };
    }

    //==========================================================================
    void EqGraph::mouseMove (const juce::MouseEvent& e)
    {
        const auto band = nodeAt (e.position);

        if (band != hovered)
        {
            hovered = band;
            repaint();
        }

        const auto overButton = rangeButton.contains (e.position);
        updateGrabPeak (band < 0 && ! overButton ? std::optional (e.position) : std::nullopt);

        setMouseCursor (band >= 0 || overButton || grabPeak.has_value() ? juce::MouseCursor::PointingHandCursor
                                                                        : juce::MouseCursor::NormalCursor);
    }

    void EqGraph::mouseExit (const juce::MouseEvent&)
    {
        updateGrabPeak (std::nullopt);

        if (hovered >= 0)
        {
            hovered = -1;
            repaint();
        }
    }

    void EqGraph::mouseDown (const juce::MouseEvent& e)
    {
        grabKeyboardFocus();

        const auto band = nodeAt (e.position);
        const auto toggle = e.mods.isCommandDown() || e.mods.isShiftDown();

        if (e.mods.isPopupMenu())
        {
            if (band >= 0)
            {
                if (! isSelected (band))
                    selectOnly (band);

                showBandMenu (band);
            }
            else
            {
                showGraphMenu (e.position);
            }

            return;
        }

        if (band < 0 && rangeButton.contains (e.position))
        {
            const auto current = std::find (rangeChoices.begin(), rangeChoices.end(), geometry.rangeDb);
            const auto next = current == rangeChoices.end() || current + 1 == rangeChoices.end() ? rangeChoices.begin()
                                                                                                    : current + 1;
            setRangeDb (*next);
            return;
        }

        if (band < 0 && grabPeak.has_value())
        {
            // Spectrum grab: put a bell on the peak and drag it straight away.
            const auto peak = *grabPeak;
            updateGrabPeak (std::nullopt);

            const auto newBand = model.findFreeBand();
            if (newBand >= 0)
            {
                const auto hz = juce::jlimit (params::range::freqMinHz, params::range::freqMaxHz, geometry.frequencyForX (peak.x));
                model.addBand (newBand, dsp::FilterType::bell, hz, 0.0f);
                refresh (false);
                selectOnly (newBand);

                dragMode = DragMode::nodes;
                dragStarted = false;
                dragBand = newBand;
                dragAnchor = dragVirtual = getNodePosition (newBand);
                lastMouse = e.position;
                lastGrabTime = juce::Time::getMillisecondCounter();
                return;
            }
        }

        if (band >= 0)
        {
            if (e.mods.isAltDown())
            {
                // Solo while the mouse is held, so a node can be swept by ear.
                soloBeforeHold = getSoloBand();
                soloHeld = true;
                setSoloBand (band);
            }

            if (toggle && isSelected (band))
            {
                auto reduced = selection;
                reduced.removeFirstMatchingValue (band);
                setSelection (reduced, reduced.isEmpty() ? -1 : reduced.getLast());
                return;
            }

            auto newSelection = toggle || isSelected (band) ? selection : juce::Array<int>();
            newSelection.addIfNotAlreadyThere (band);
            setSelection (newSelection, band);

            dragMode = DragMode::nodes;
            dragStarted = false;
            dragBand = band;
            dragAnchor = dragVirtual = getNodePosition (band);
            lastMouse = e.position;
            return;
        }

        dragMode = DragMode::lasso;
        lassoStart = e.position;
        lasso = {};
        selectionBeforeLasso = toggle ? selection : juce::Array<int>();
    }

    void EqGraph::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragMode == DragMode::nodes)
        {
            if (! dragStarted)
            {
                if (e.getDistanceFromDragStart() < 2)
                    return;

                beginNodeDrag();
            }

            // Track a virtual pointer so Shift can slow the motion down
            // without the node jumping when it is pressed or released.
            const auto speed = e.mods.isShiftDown() ? 0.12f : 1.0f;
            dragVirtual += (e.position - lastMouse) * speed;
            lastMouse = e.position;
            applyNodeDrag();
        }
        else if (dragMode == DragMode::lasso)
        {
            lasso = juce::Rectangle<float> (lassoStart, e.position);

            auto newSelection = selectionBeforeLasso;
            for (auto b : nodesIn (lasso))
                newSelection.addIfNotAlreadyThere (b);

            setSelection (newSelection, newSelection.contains (primary) ? primary
                                                                        : newSelection.isEmpty() ? -1 : newSelection.getLast());
        }
    }

    void EqGraph::mouseUp (const juce::MouseEvent& e)
    {
        if (dragMode == DragMode::nodes && dragStarted)
            endNodeDrag();

        if (soloHeld)
        {
            soloHeld = false;
            setSoloBand (soloBeforeHold);
        }

        if (dragMode == DragMode::lasso)
        {
            if (lasso.isEmpty() && ! e.mouseWasDraggedSinceMouseDown() && selectionBeforeLasso.isEmpty())
                setSelection ({}, -1);

            lasso = {};
            repaint();
        }

        dragMode = DragMode::none;
    }

    void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;

        const auto band = nodeAt (e.position);

        // The second click of a double-click on a spectrum peak lands on the
        // band the first click just grabbed; keep it.
        if (band >= 0 && juce::Time::getMillisecondCounter() - lastGrabTime < 600)
            return;

        if (band >= 0)
            removeBands ({ band });
        else if (! rangeButton.contains (e.position))
            addBandAt (e.position);
    }

    void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        const auto band = nodeAt (e.position);

        if (band < 0)
        {
            Component::mouseWheelMove (e, wheel);
            return;
        }

        const auto delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;

        if (juce::exactlyEqual (delta, 0.0f))
            return;

        for (auto b : bandsToEdit (band))
        {
            const auto& s = bands[(size_t) b];

            if (e.mods.isAltDown())
            {
                if (EqModel::usesSlope (s.type))
                {
                    const auto step = delta > 0.0f ? 1 : -1;
                    model.set (b, BandParam::slope,
                               (float) juce::jlimit (0, dsp::numSlopes - 1, s.slopeIndex + step));
                }
            }
            else
            {
                const auto q = s.q * std::pow (2.0f, delta * 1.5f);
                model.set (b, BandParam::q, juce::jlimit (params::range::qMin, params::range::qMax, q));
            }
        }

        refresh (false);
    }

    bool EqGraph::keyPressed (const juce::KeyPress& key)
    {
        if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && ! selection.isEmpty())
        {
            removeBands (selection);
            return true;
        }

        if (key == juce::KeyPress::escapeKey && ! selection.isEmpty())
        {
            setSelection ({}, -1);
            return true;
        }

        if (key == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0) && ! selection.isEmpty())
        {
            auto sorted = selection;
            sorted.sort();
            BandClipboard::copy (model, sorted);
            return true;
        }

        if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0))
        {
            pasteBands();
            return true;
        }

        if (key == juce::KeyPress::returnKey && primary >= 0)
        {
            if (onEnterValues) onEnterValues (primary);
            return true;
        }

        if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
        {
            juce::Array<int> all;
            for (int b = 0; b < dsp::maxBands; ++b)
                if (bands[(size_t) b].enabled)
                    all.add (b);

            setSelection (all, all.contains (primary) ? primary : all.isEmpty() ? -1 : all.getLast());
            return true;
        }

        return false;
    }

    //==========================================================================
    void EqGraph::beginNodeDrag()
    {
        dragStarted = true;
        dragStarts.clear();

        for (auto b : bandsToEdit (dragBand))
        {
            const auto& s = bands[(size_t) b];
            const auto dragsGain = EqModel::usesGain (s.type);
            dragStarts.push_back ({ b, s.frequency, s.gainDb, s.q, dragsGain });

            model.beginGesture (b, BandParam::frequency);
            model.beginGesture (b, dragsGain ? BandParam::gain : BandParam::q);
        }
    }

    void EqGraph::applyNodeDrag()
    {
        const auto ratio = geometry.frequencyForX (dragVirtual.x) / geometry.frequencyForX (dragAnchor.x);
        // Nodes sit at the scaled gain, so undo the scale to keep them under the mouse.
        const auto scale = std::abs (gainScale) < 0.05f ? 1.0f : gainScale;
        const auto dbDelta = (geometry.dbForY (dragVirtual.y) - geometry.dbForY (dragAnchor.y)) / scale;

        // For bands without gain, moving up sharpens the Q: one octave per 60 px.
        const auto qFactor = std::pow (2.0f, (dragAnchor.y - dragVirtual.y) / 60.0f);

        for (const auto& start : dragStarts)
        {
            model.set (start.band, BandParam::frequency, snapFrequency (start.frequency * ratio));

            if (start.dragsGain)
                model.set (start.band, BandParam::gain, juce::jlimit (-maxGainDb, maxGainDb, start.gainDb + dbDelta));
            else
                model.set (start.band, BandParam::q, juce::jlimit (params::range::qMin, params::range::qMax, start.q * qFactor));
        }

        refresh (false);
    }

    void EqGraph::endNodeDrag()
    {
        for (const auto& start : dragStarts)
        {
            model.endGesture (start.band, BandParam::frequency);
            model.endGesture (start.band, start.dragsGain ? BandParam::gain : BandParam::q);
        }

        dragStarts.clear();
        dragStarted = false;
    }

    void EqGraph::removeBands (const juce::Array<int>& toRemove)
    {
        for (auto b : toRemove)
            model.removeBand (b);

        hovered = -1;
        refresh (false);
    }

    void EqGraph::pasteBands()
    {
        const auto pasted = BandClipboard::pasteAsNew (model);
        refresh (false);

        if (! pasted.isEmpty())
            setSelection (pasted, pasted.getLast());
    }

    void EqGraph::addBandAt (juce::Point<float> pos)
    {
        const auto band = model.findFreeBand();

        if (band < 0)
            return;

        const auto hz = snapFrequency (geometry.frequencyForX (pos.x));
        const auto db = juce::jlimit (-maxGainDb, maxGainDb, geometry.dbForY (pos.y));

        // Below and above the audible range a cut is almost always what is wanted.
        const auto type = hz < 20.0f    ? dsp::FilterType::lowCut
                        : hz > 20000.0f ? dsp::FilterType::highCut
                                        : dsp::FilterType::bell;

        model.addBand (band, type, hz, std::round (db * 10.0f) / 10.0f);
        refresh (false);
        selectOnly (band);
    }

    void EqGraph::showBandMenu (int band)
    {
        const auto& s = bands[(size_t) band];
        const auto targets = bandsToEdit (band);

        juce::PopupMenu menu;
        menu.addSectionHeader (targets.size() > 1 ? juce::String (targets.size()) + " bands"
                                                  : "Band " + juce::String (band + 1));

        const auto typeNames = params::filterTypeNames();
        for (int t = 0; t < typeNames.size(); ++t)
            menu.addItem (100 + t, typeNames[t], true, (int) s.type == t);

        juce::PopupMenu slopes;
        const auto slopeNames = params::slopeNames();
        for (int i = 0; i < slopeNames.size(); ++i)
            slopes.addItem (200 + i, slopeNames[i], true, s.slopeIndex == i);
        menu.addSubMenu ("Slope", slopes, EqModel::usesSlope (s.type));

        juce::PopupMenu placements;
        const auto placementNames = params::placementNames();
        for (int i = 0; i < placementNames.size(); ++i)
            placements.addItem (300 + i, placementNames[i], true, (int) s.placement == i);
        menu.addSubMenu ("Placement", placements);

        menu.addSeparator();
        menu.addItem (4, "Enter values...", targets.size() == 1);
        menu.addItem (2, targets.size() > 1 ? "Copy bands" : "Copy band");
        menu.addItem (3, "Paste settings onto band", targets.size() == 1 && BandClipboard::count() > 0);
        menu.addSeparator();
        menu.addItem (1, targets.size() > 1 ? "Remove bands" : "Remove band");

        juce::Component::SafePointer<EqGraph> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                     .withMousePosition(),
                            [safeThis, targets] (int result)
                            {
                                if (safeThis == nullptr || result == 0)
                                    return;

                                auto& m = safeThis->model;

                                if (result == 2)
                                {
                                    auto sorted = targets;
                                    sorted.sort();
                                    BandClipboard::copy (m, sorted);
                                    return;
                                }

                                if (result == 3 || result == 4)
                                {
                                    if (result == 3)
                                        BandClipboard::pasteOnto (m, targets.getFirst());
                                    else if (safeThis->onEnterValues)
                                        safeThis->onEnterValues (targets.getFirst());

                                    safeThis->refresh (false);
                                    return;
                                }

                                for (auto b : targets)
                                {
                                    if (result == 1)                             m.removeBand (b);
                                    else if (result >= 300)                      m.set (b, BandParam::placement, (float) (result - 300));
                                    else if (result >= 200)                      m.set (b, BandParam::slope, (float) (result - 200));
                                    else if (result >= 100)                      m.set (b, BandParam::type, (float) (result - 100));
                                }

                                safeThis->refresh (false);
                            });
    }

    void EqGraph::showGraphMenu (juce::Point<float> pos)
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Add band here", model.findFreeBand() >= 0);

        juce::PopupMenu ranges;
        for (size_t i = 0; i < rangeChoices.size(); ++i)
            ranges.addItem (10 + (int) i, juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String ((int) rangeChoices[i]) + " dB",
                            true, juce::exactlyEqual (rangeChoices[i], geometry.rangeDb));
        menu.addSubMenu ("Display range", ranges);
        menu.addItem (3, "Piano roll (snap to notes)", true, model.isPianoRollOn());

        bool anyEnabled = false;
        for (const auto& s : bands)
            anyEnabled = anyEnabled || s.enabled;

        const auto clipboardBands = BandClipboard::count();

        menu.addSeparator();
        menu.addItem (4, clipboardBands > 1 ? "Paste " + juce::String (clipboardBands) + " bands" : "Paste band",
                      clipboardBands > 0 && model.findFreeBand() >= 0);
        menu.addItem (2, "Remove all bands", anyEnabled);

        juce::Component::SafePointer<EqGraph> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [safeThis, pos] (int result)
                            {
                                if (safeThis == nullptr)
                                    return;

                                if (result == 1)
                                {
                                    safeThis->addBandAt (pos);
                                }
                                else if (result == 4)
                                {
                                    safeThis->pasteBands();
                                }
                                else if (result == 3)
                                {
                                    safeThis->model.setPianoRoll (! safeThis->model.isPianoRollOn());
                                    safeThis->refresh (false);
                                }
                                else if (result == 2)
                                {
                                    juce::Array<int> all;
                                    for (int b = 0; b < dsp::maxBands; ++b)
                                        all.add (b);
                                    safeThis->removeBands (all);
                                }
                                else if (result >= 10)
                                {
                                    safeThis->setRangeDb (rangeChoices[(size_t) (result - 10)]);
                                }
                            });
    }

    //==========================================================================
    void EqGraph::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (colours::graphTop, 0.0f, bounds.getY(),
                                                 colours::graphBottom, 0.0f, bounds.getBottom(), false));
        g.fillRect (bounds);

        drawGrid (g);
    }

    void EqGraph::paintOverChildren (juce::Graphics& g)
    {
        if (pianoRollShown)
            drawPianoRoll (g);

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (getLocalBounds().withTrimmedBottom (22));
            drawCurves (g);
        }

        drawNodes (g);

        const auto readoutBand = dragMode == DragMode::nodes && dragStarted ? dragBand : hovered;
        if (readoutBand >= 0 && bands[(size_t) readoutBand].enabled)
            drawReadout (g, readoutBand);

        if (! lasso.isEmpty())
        {
            g.setColour (colours::text.withAlpha (0.08f));
            g.fillRect (lasso);
            g.setColour (colours::text.withAlpha (0.4f));
            g.drawRect (lasso, 1.0f);
        }

        drawRangeButton (g);
        drawSoloBanner (g);

        bool anyEnabled = false;
        for (const auto& s : bands)
            anyEnabled = anyEnabled || s.enabled;

        if (! anyEnabled)
        {
            g.setColour (colours::textDim.withAlpha (0.45f));
            g.setFont (juce::FontOptions (15.0f));
            g.drawText ("Double-click anywhere to add a band", plot.withTrimmedTop (plot.getHeight() * 0.25f).withHeight (24.0f),
                        juce::Justification::centred);
        }
    }

    void EqGraph::drawGrid (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        // Frequency lines: 1-2-3...9 per decade, decades brighter.
        for (float decade = 10.0f; decade <= 10000.0f; decade *= 10.0f)
        {
            for (int i = 1; i <= 9; ++i)
            {
                const auto hz = decade * (float) i;
                if (hz < geometry.minHz || hz > geometry.maxHz)
                    continue;

                const auto x = geometry.xForFrequency (hz);
                g.setColour (i == 1 ? colours::gridMajor : colours::gridMinor);
                g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            }
        }

        g.setFont (juce::FontOptions (11.0f));
        g.setColour (colours::gridText);

        for (auto hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const auto x = geometry.xForFrequency (hz);
            g.drawText (frequencyLabel (hz), juce::Rectangle<float> (x - 24.0f, plot.getBottom() + 3.0f, 48.0f, 16.0f),
                        juce::Justification::centred);
        }

        // Gain lines in whole-dB steps that suit the range.
        const auto step = geometry.rangeDb <= 3.0f ? 1.0f : geometry.rangeDb <= 6.0f ? 2.0f
                        : geometry.rangeDb <= 12.0f ? 3.0f : 6.0f;
        const auto lines = juce::roundToInt (geometry.rangeDb / step);

        for (int i = -lines; i <= lines; ++i)
        {
            const auto y = geometry.yForDb (step * (float) i);

            g.setColour (i == 0 ? colours::gridMajor.withMultipliedAlpha (1.6f) : colours::gridMinor);
            g.drawHorizontalLine (juce::roundToInt (y), bounds.getX(), bounds.getRight());

            if (i == lines) // the range button sits there
                continue;

            const auto db = juce::roundToInt (step * (float) i);
            g.setColour (colours::gridText);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<float> (bounds.getRight() - 46.0f, y - 15.0f, 40.0f, 14.0f),
                        juce::Justification::bottomRight);
        }
    }

    juce::Path EqGraph::curvePath (const std::vector<float>& db) const
    {
        juce::Path path;

        for (size_t i = 0; i < db.size(); ++i)
        {
            // Clamp far-off values so very deep cuts do not make huge paths.
            const auto y = geometry.yForDb (juce::jlimit (-geometry.rangeDb * 4.0f, geometry.rangeDb * 4.0f, db[i]));

            if (i == 0) path.startNewSubPath (pointX[i], y);
            else        path.lineTo (pointX[i], y);
        }

        return path;
    }

    void EqGraph::drawCurves (juce::Graphics& g)
    {
        if (! curvesValid)
            return;

        const auto zeroY = geometry.yForDb (0.0f);

        // Individual bands: the selected and hovered ones are filled.
        for (int b = 0; b < dsp::maxBands; ++b)
        {
            if (! bands[(size_t) b].enabled || bandDb[(size_t) b].empty())
                continue;

            const auto colour = bandColour (b);
            const auto emphasised = isSelected (b) || b == hovered;
            auto path = curvePath (bandDb[(size_t) b]);

            if (emphasised)
            {
                auto fill = path;
                fill.lineTo (pointX.back(), zeroY);
                fill.lineTo (pointX.front(), zeroY);
                fill.closeSubPath();

                g.setColour (colour.withAlpha (b == primary ? 0.22f : 0.14f));
                g.fillPath (fill);
            }

            g.setColour (colour.withAlpha (emphasised ? 0.85f : 0.35f));
            g.strokePath (path, juce::PathStrokeType (emphasised ? 1.5f : 1.0f));
        }

        // Channel-specific curves, when any band works on L, R, M or S.
        for (int p = 1; p < dsp::numPlacements; ++p)
        {
            if (! placementUsed[(size_t) p])
                continue;

            const auto path = curvePath (placementDb[(size_t) p]);
            const auto colour = colours::curve.withAlpha (0.55f);

            juce::Path dashed;
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, path, dashes, 2);
            g.setColour (colour);
            g.fillPath (dashed);

            // Label the curve where it differs most from the stereo one.
            const auto& curve = placementDb[(size_t) p];
            const auto& stereo = placementDb[(size_t) dsp::Placement::stereo];
            size_t widest = 0;
            for (size_t i = 0; i < curve.size(); ++i)
                if (std::abs (curve[i] - stereo[i]) > std::abs (curve[widest] - stereo[widest]))
                    widest = i;

            const auto above = curve[widest] >= stereo[widest];
            const auto labelY = juce::jlimit (plot.getY(), plot.getBottom() - 14.0f,
                                              geometry.yForDb (curve[widest]) + (above ? -30.0f : 16.0f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (placementLetter (p), juce::Rectangle<float> (pointX[widest] - 7.0f, labelY, 14.0f, 14.0f),
                        juce::Justification::centred);
        }

        // The combined response of all stereo bands, with a soft glow.
        const auto total = curvePath (placementDb[(size_t) dsp::Placement::stereo]);
        g.setColour (colours::curve.withAlpha (0.12f));
        g.strokePath (total, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colours::curve);
        g.strokePath (total, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void EqGraph::drawNodes (juce::Graphics& g)
    {
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));

        for (int b = 0; b < dsp::maxBands; ++b)
        {
            if (! bands[(size_t) b].enabled)
                continue;

            const auto centre = getNodePosition (b);
            const auto colour = bandColour (b);
            const auto selected = isSelected (b);
            const auto radius = nodeRadius + (b == hovered ? 1.5f : 0.0f);
            const auto circle = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

            if (selected)
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillEllipse (circle.expanded (5.0f));
            }

            if (b == lastSolo)
            {
                g.setColour (colours::solo);
                g.drawEllipse (circle.expanded (4.0f), 2.0f);
            }

            g.setColour (selected ? colour : colour.withMultipliedSaturation (0.8f).darker (0.15f));
            g.fillEllipse (circle);

            g.setColour (selected || b == hovered ? juce::Colours::white : juce::Colours::black.withAlpha (0.5f));
            g.drawEllipse (circle, selected ? 2.0f : 1.0f);

            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.drawText (juce::String (b + 1), circle, juce::Justification::centred);
        }
    }

    void EqGraph::drawReadout (juce::Graphics& g, int band)
    {
        const auto& s = bands[(size_t) band];

        auto text = model.parameter (band, BandParam::frequency).getCurrentValueAsText();

        if (EqModel::usesGain (s.type))
            text << "   " << model.parameter (band, BandParam::gain).getCurrentValueAsText();
        else if (EqModel::usesSlope (s.type))
            text << "   " << model.parameter (band, BandParam::slope).getCurrentValueAsText();

        if (EqModel::usesQ (s))
            text << "   Q " << model.parameter (band, BandParam::q).getCurrentValueAsText();

        if (pianoRollShown)
            text << "   " << dsp::noteName ((int) std::round (dsp::noteForFrequency (s.frequency)));

        const juce::Font font (juce::FontOptions (12.0f));
        const auto width = (float) juce::GlyphArrangement::getStringWidthInt (font, text) + 16.0f;
        const auto node = getNodePosition (band);

        auto box = juce::Rectangle<float> (width, 22.0f).withCentre ({ node.x, node.y - nodeRadius - 18.0f });
        if (box.getY() < 2.0f)
            box.setY (node.y + nodeRadius + 8.0f);
        box = box.constrainedWithin (getLocalBounds().toFloat().reduced (2.0f));

        g.setColour (juce::Colour (0xe0101217));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (bandColour (band).withAlpha (0.8f));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (colours::text);
        g.setFont (font);
        g.drawText (text, box, juce::Justification::centred);
    }

    void EqGraph::drawSoloBanner (juce::Graphics& g)
    {
        if (lastSolo < 0)
            return;

        const auto box = juce::Rectangle<float> (plot.getX() + 8.0f, 6.0f, 96.0f, 20.0f);
        g.setColour (colours::solo.withAlpha (0.9f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("SOLO  Band " + juce::String (lastSolo + 1), box, juce::Justification::centred);
    }

    float EqGraph::snapFrequency (float hz) const
    {
        if (model.isPianoRollOn())
            hz = (float) dsp::snapToNote (hz);

        return juce::jlimit (params::range::freqMinHz, params::range::freqMaxHz, hz);
    }

    void EqGraph::drawPianoRoll (juce::Graphics& g)
    {
        constexpr float keyHeight = 12.0f;

        const auto keys = juce::Rectangle<float> (plot.getX(), plot.getBottom() - keyHeight, plot.getWidth(), keyHeight);
        const auto lowest  = (int) std::floor (dsp::noteForFrequency (geometry.minHz));
        const auto highest = (int) std::ceil (dsp::noteForFrequency (geometry.maxHz));

        g.setFont (juce::FontOptions (9.0f));

        for (int note = lowest; note <= highest; ++note)
        {
            const auto left  = geometry.xForFrequency ((float) dsp::frequencyForNote (note - 0.5));
            const auto right = geometry.xForFrequency ((float) dsp::frequencyForNote (note + 0.5));
            const auto black = dsp::isBlackKey (note);

            // Faint lanes for the black keys, like a piano roll.
            if (black)
            {
                g.setColour (juce::Colours::black.withAlpha (0.12f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (left, plot.getY(), right, keys.getY()));
            }

            // White keys fill the strip; black keys sit on top, and a line
            // marks the two places where white keys meet (B-C and E-F).
            g.setColour (juce::Colour (0xffb8bcc6));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (left, keys.getY(), right, keys.getBottom()));

            g.setColour (juce::Colour (0xff15171c));
            if (black)
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (left, keys.getY(), right, keys.getY() + keyHeight * 0.6f));
            else if (! dsp::isBlackKey (note - 1))
                g.drawVerticalLine (juce::roundToInt (left), keys.getY(), keys.getBottom());

            if (note % 12 == 0)
            {
                g.setColour (colours::gridText);
                g.drawVerticalLine (juce::roundToInt (left), plot.getY(), keys.getY());
                g.drawText (dsp::noteName (note), juce::Rectangle<float> (left + 2.0f, keys.getY() - 12.0f, 30.0f, 11.0f),
                            juce::Justification::centredLeft);
            }
        }
    }

    void EqGraph::drawRangeButton (juce::Graphics& g)
    {
        g.setColour (colours::control.withAlpha (0.85f));
        g.fillRoundedRectangle (rangeButton, 4.0f);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String ((int) geometry.rangeDb) + " dB", rangeButton,
                    juce::Justification::centred);
    }
}
