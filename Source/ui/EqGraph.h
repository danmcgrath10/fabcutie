#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "EqModel.h"
#include "GraphGeometry.h"

namespace fabcutie::ui
{
    // The main frequency display: grid, per-band and combined response
    // curves, and a draggable node for every active band.
    //
    // Mouse:
    //   double-click empty space   add a band there (cuts at the far ends)
    //   drag a node                frequency and gain (or Q for cut, notch
    //                              and band pass); hold Shift for fine moves
    //   scroll over a node         Q (Alt/Option + scroll: cut slope)
    //   double-click a node        remove the band
    //   Cmd/Ctrl/Shift-click       add or remove a node from the selection
    //   drag empty space           lasso-select nodes
    //   right-click                band or graph menu
    //   Delete / Backspace         remove the selected bands
    //
    // Painting is layered so the spectrum analyzer can slot in later: paint()
    // draws the background and grid, child components (setBackgroundLayer)
    // draw above that, and paintOverChildren() draws the curves and nodes.
    class EqGraph final : public juce::Component,
                          private juce::Timer
    {
    public:
        EqGraph (EqModel&, std::function<double()> sampleRateSource);
        ~EqGraph() override;

        // Adds a component that is drawn behind the curves and ignores the
        // mouse, e.g. a spectrum analyzer. Pass nullptr to remove it.
        void setBackgroundLayer (juce::Component* layer);

        float getRangeDb() const noexcept { return geometry.rangeDb; }
        void setRangeDb (float rangeDb);
        static constexpr std::array<float, 4> rangeChoices { 3.0f, 6.0f, 12.0f, 30.0f };

        // The band the floating panel edits: the last one clicked. -1 if none.
        int getPrimaryBand() const noexcept { return primary; }
        void selectOnly (int band);

        juce::Point<float> getNodePosition (int band) const;

        std::function<void()> onSelectionChanged; // primary band changed
        std::function<void()> onBandsChanged;     // any band value changed
        std::function<void (float)> onRangeChanged;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        using BandParam = params::BandParam;

        enum class DragMode { none, nodes, lasso };

        struct DragStart
        {
            int band;
            float frequency, gainDb, q;
            bool dragsGain;
        };

        void timerCallback() override;
        void refresh (bool force);
        void recomputeCurves();

        float nodeDb (const dsp::BandSettings&) const noexcept;
        int nodeAt (juce::Point<float>) const;
        juce::Array<int> nodesIn (juce::Rectangle<float>) const;
        bool isSelected (int band) const noexcept { return selection.contains (band); }
        void setSelection (const juce::Array<int>& bands, int newPrimary);
        juce::Array<int> bandsToEdit (int band) const;

        void beginNodeDrag();
        void applyNodeDrag();
        void endNodeDrag();

        void removeBands (const juce::Array<int>& bands);
        void addBandAt (juce::Point<float>);
        void showBandMenu (int band);
        void showGraphMenu (juce::Point<float>);

        void drawGrid (juce::Graphics&);
        juce::Path curvePath (const std::vector<float>& db) const;
        void drawCurves (juce::Graphics&);
        void drawNodes (juce::Graphics&);
        void drawReadout (juce::Graphics&, int band);
        void drawRangeButton (juce::Graphics&);

        EqModel& model;
        std::function<double()> sampleRateSource;
        juce::Component* backgroundLayer = nullptr;

        GraphGeometry geometry;
        juce::Rectangle<float> plot, rangeButton;

        // Snapshot of the band values the curves were computed from.
        std::array<dsp::BandSettings, dsp::maxBands> bands {};
        double curveSampleRate = 0.0;
        bool curvesValid = false;

        std::vector<float> pointX, pointHz;
        std::array<std::vector<float>, dsp::maxBands> bandDb;
        std::array<std::vector<float>, dsp::numPlacements> placementDb; // stereo bands + that placement's bands
        std::array<bool, dsp::numPlacements> placementUsed {};

        juce::Array<int> selection;
        int primary = -1;
        int hovered = -1;

        DragMode dragMode = DragMode::none;
        bool dragStarted = false;
        int dragBand = -1;
        juce::Point<float> dragAnchor, dragVirtual, lastMouse;
        std::vector<DragStart> dragStarts;

        juce::Point<float> lassoStart;
        juce::Rectangle<float> lasso;
        juce::Array<int> selectionBeforeLasso;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqGraph)
    };
}
