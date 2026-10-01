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
- `MatchPanel.*`: EQ Match: reference choice, capture, learn, amount, band count, preview and Apply.
