# DSP

Audio-thread code lives here, independent of the UI.

- `EqTypes.h`: band settings, filter types, slopes and placements shared with the parameters.
- `FilterDesign.h`: turns band settings into state variable filter sections, and computes
  the exact magnitude response (for tests now, for drawing the EQ curve in the UI later).
- `EqBand.h`: one band: smoothed frequency/gain/Q, click-free switching of type, slope,
  placement and on/off, and the dynamic gain offset applied on top of the static gain.
- `BandDynamics.h`: a dynamic band's detector (sidechain filter, peak level with release,
  linked across channels) and gain computer (threshold, range, soft knee, attack).
- `EqEngine.h`: 24 bands in series with per-band stereo/left/right/mid/side routing, and the
  external sidechain buffer for dynamic bands that listen to it.
- `OutputStage.h`: smoothed output gain and bypass.
- Planned: linear-phase mode.
