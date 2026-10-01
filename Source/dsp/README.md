# DSP

Audio-thread code lives here, independent of the UI.

- `OutputStage.h`: smoothed output gain and bypass.
- Planned: `EqBand` / `EqEngine` (bell, shelf, cut, notch, tilt; up to 96 dB/oct;
  stereo or mid/side per band), dynamic EQ, linear-phase mode.
