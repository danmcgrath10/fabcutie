#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

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
    //   Alt/Option-click a node    solo the band while the mouse is held
    //   click a spectrum peak      add a bell there and drag it (spectrum grab)
    //
    // With the piano roll on (graph menu), a keyboard runs along the bottom
    // and dragged or added bands snap to the nearest note.
    //
    // In sketch mode (EQ Sketch), dragging draws the curve you want instead;
    // on release it is turned into bands in the free slots. Escape leaves
    // sketch mode.
    //
    // Painting is layered: paint() draws the background and grid, child
    // components (setBackgroundLayer, e.g. the spectrum analyzer) draw above
    // that, and paintOverChildren() draws the curves and nodes.
    class EqGraph final : public juce::Component,
                          private juce::Timer
    {
    public:
        EqGraph (EqModel&, std::function<double()> sampleRateSource);
        ~EqGraph() override;

        // Offers peaks of the analyzer's spectrum to grab with the mouse.
        class PeakSource
        {
        public:
            virtual ~PeakSource() = default;

            // The peak to grab near a mouse position, in graph coordinates.
            virtual std::optional<juce::Point<float>> findPeakNear (juce::Point<float>) = 0;

            // Shows (or, with nullopt, hides) the peak the mouse would grab.
            virtual void setHighlightedPeak (std::optional<juce::Point<float>>) = 0;
        };

        void setPeakSource (PeakSource* source) noexcept { peakSource = source; }

        // Where the soloed band is kept (shared with the audio thread).
        void setSoloTarget (std::atomic<int>* target) noexcept { soloTarget = target; }

        const GraphGeometry& getGeometry() const noexcept { return geometry; }
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }

        // Adds a component that is drawn behind the curves and ignores the
        // mouse, e.g. a spectrum analyzer. Pass nullptr to remove it.
        void setBackgroundLayer (juce::Component* layer);

        // Other instances' combined curves drawn faintly behind this one's
        // (instance list), each in its own colour and labelled with its name.
        struct Overlay
        {
            juce::String name;
            juce::Colour colour;
            std::array<dsp::BandSettings, dsp::maxBands> bands {};
        };

        void setOverlays (std::vector<Overlay>);

        // Names placements by speaker side (surround) instead of mid/side.
        void setSurround (bool);

        // Natural and linear phase draw the analog curves the FIR runs
        // (dynamic bands stay IIR, so they keep the digital curve).
        void setPhaseMode (dsp::PhaseMode mode) noexcept
        {
            if (mode != phaseMode)
            {
                phaseMode = mode;
                curvesValid = false;
            }
        }

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

        // EQ Sketch.
        void setSketchMode (bool shouldSketch);
        bool isSketchMode() const noexcept { return sketchMode; }
        std::function<void (bool)> onSketchModeChanged;
        static constexpr int sketchMaxBands = 8;

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

        enum class DragMode { none, nodes, lasso, sketch };

        struct DragStart
        {
            int band;
            float frequency, gainDb, q;
            bool dragsGain;
        };

        void timerCallback() override;
        int getSoloBand() const noexcept;
        void setSoloBand (int band);
        void updateGrabPeak (std::optional<juce::Point<float>> mouse);
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
        void drawOverlays (juce::Graphics&);
        void recomputeOverlays();
        void drawReadout (juce::Graphics&, int band);
        void drawRangeButton (juce::Graphics&);
        void drawSoloBanner (juce::Graphics&);
        void drawPianoRoll (juce::Graphics&);
        void drawSketch (juce::Graphics&);
        void sketchTo (juce::Point<float>);
        void finishSketch();
        float snapFrequency (float hz) const;

        EqModel& model;
        std::function<double()> sampleRateSource;
        juce::Component* backgroundLayer = nullptr;
        PeakSource* peakSource = nullptr;
        std::atomic<int>* soloTarget = nullptr;

        std::optional<juce::Point<float>> grabPeak;
        juce::uint32 lastGrabTime = 0;
        int lastSolo = -1;
        int soloBeforeHold = -1;
        bool soloHeld = false;

        GraphGeometry geometry;
        juce::Rectangle<float> plot, rangeButton;

        // Snapshot of the band values the curves were computed from.
        std::array<dsp::BandSettings, dsp::maxBands> bands {};
        double curveSampleRate = 0.0;
        bool curvesValid = false;
        dsp::PhaseMode phaseMode = dsp::PhaseMode::zeroLatency;
        bool pianoRollShown = false;
        bool surround = false;

        std::vector<float> pointX, pointHz;
        std::array<std::vector<float>, dsp::maxBands> bandDb;
        std::array<std::vector<float>, dsp::numPlacements> placementDb; // stereo bands + that placement's bands
        std::array<bool, dsp::numPlacements> placementUsed {};

        std::vector<Overlay> overlays;
        std::vector<std::vector<float>> overlayDb;

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

        // The drawn curve, one value per pixel column of the graph (NaN
        // where nothing was drawn).
        bool sketchMode = false;
        std::vector<float> sketchDb;
        int lastSketchColumn = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqGraph)
    };
}
