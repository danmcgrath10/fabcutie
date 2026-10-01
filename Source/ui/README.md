# UI

Editor components.

- `Theme.*`: colours, per-band colours and the look and feel.
- `EqModel.*`: the editor's read/write access to the band parameters (writes go through the host with change gestures).
- `GraphGeometry.h`: pixel <-> frequency/dB mapping (unit tested).
- `EqGraph.*`: the frequency display: grid, band and combined curves, draggable nodes, menus.
- `BandPanel.*`: the floating controls for the selected band.

The spectrum analyzer will plug into `EqGraph::setBackgroundLayer`, which draws a
component between the grid and the curves.
