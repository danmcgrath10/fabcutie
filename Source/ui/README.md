# UI

Editor components.

- `Theme.*`: colours, per-band colours and the look and feel.
- `EqModel.*`: the editor's read/write access to the band parameters (writes go through the host with change gestures).
- `GraphGeometry.h`: pixel <-> frequency/dB mapping (unit tested).
- `EqGraph.*`: the frequency display: grid, band and combined curves, draggable nodes, menus,
  and EQ Sketch drawing.
- `BandPanel.*`: the floating controls for the selected band, including Solo.
- `SpectrumDisplay.*`: the pre/post/sidechain spectrum and collision shading, drawn as
  `EqGraph`'s background layer (between the grid and the curves). It also offers peaks
  to the graph for spectrum grab (`EqGraph::PeakSource`).
- `AnalyzerSettings.h`: analyzer range, speed, tilt, resolution etc., saved in the state tree.
- `AnalyzerBar.*`: the toggles and settings menu under the graph.
- `LevelMeter.*`: the output meter.
- `WorkflowBar.*`: the header's undo / redo, preset browser, A/B and settings menu (window size, MIDI).
- `OutputBar.*`: gain scale, auto gain and phase invert, beside the analyzer bar.
- `BandClipboard.*`: copy and paste of band settings through the system clipboard.
- `ValueEntry.*`: the box for typing a band's frequency, gain and Q.
- `MidiLearnMenu.*`: the right-click MIDI learn menu on knobs and menus.
- `InstanceList.*`: the instance list panel (show another instance's curve, edit it here, rename).
  The editor swaps everything bound to one instance (model, graph, band panel, analyzer, meter,
  header attachments) when it is pointed at another; `EqGraph::setOverlays` draws the shown curves.
- `MatchPanel.*`: EQ Match: reference choice, capture, learn, amount, band count, preview and Apply.
- `AssistPanel.*`: Assist: resonance finder and unmasking (key choice, learn, preview, Apply).

The undo history, A/B slots, presets and MIDI map live in `Source/workflow/` and are owned by the processor, so they outlive the window.
