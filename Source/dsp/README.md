# DSP

Audio-thread code lives here, independent of the UI.

- `EqTypes.h`: band settings, filter types, slopes and placements shared with the parameters.
- `FilterDesign.h`: turns band settings into state variable filter sections, and computes
  the exact magnitude response (for tests now, for drawing the EQ curve in the UI later).
- `EqBand.h`: one band: smoothed frequency/gain/Q, click-free switching of type, slope,
  placement and on/off.
- `EqEngine.h`: 24 bands in series with per-band stereo/left/right/mid/side routing.
- `Character.h`: global Clean/Gentle/Warm saturation after the bands, run at 4x oversampling
  so its harmonics don't alias. Unity gain for quiet signals; Clean is a bit-exact bypass.
- `OutputStage.h`: smoothed output gain and bypass.
- Planned: dynamic EQ, linear-phase mode.
