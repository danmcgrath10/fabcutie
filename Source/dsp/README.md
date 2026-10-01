# DSP

Audio-thread code lives here, independent of the UI.

- `EqTypes.h`: band settings, filter types, slopes and placements shared with the parameters.
- `FilterDesign.h`: turns band settings into state variable filter sections, and computes
  the exact magnitude response (for tests now, for drawing the EQ curve in the UI later).
- `EqBand.h`: one band: smoothed frequency/gain/Q, click-free switching of type, slope,
  placement and on/off.
- `EqEngine.h`: 24 bands in series with per-band stereo/left/right/mid/side routing.
- `OutputStage.h`: smoothed output gain and bypass.
- `AudioTap.h`: lock-free mono feed from the audio thread to the analyzer.
- `SpectrumAnalyzer.h`: FFT, windowing and smoothing for the analyzer (runs on the message thread).
- `PeakMeter.h`: per-channel peak capture for the output meter.
- `BandSolo.h`: intelligent solo, auditions the region a band works on.
- `EditorLink.h`: the taps, meter, sidechain flag and solo band shared with the editor.
- Planned: dynamic EQ, linear-phase mode.
