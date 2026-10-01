# DSP

Audio-thread code lives here, independent of the UI.

- `EqTypes.h`: band settings, filter types, slopes and placements shared with the parameters.
- `FilterDesign.h`: turns band settings into state variable filter sections, and computes
  the exact response the processor runs (used by the tests and to draw the EQ curve).
  Besides the classic shapes it builds the All Pass (one 2-pole all-pass section),
  Flat Tilt (16 first-order pole/zero pairs refined into a straight dB/oct line, gain =
  level change across 20 Hz to 20 kHz) and the Brickwall cut slope (16th-order inverse
  Chebyshev, -3 dB at the cutoff, 100 dB stop band).
- `Notes.h`: equal-tempered note maths for the piano roll (A4 = 440 Hz).
- `EqBand.h`: one band: smoothed frequency/gain/Q, click-free switching of type, slope,
  placement and on/off, and the dynamic gain offset applied on top of the static gain.
- `BandDynamics.h`: a dynamic band's detector (sidechain filter, peak level with release,
  linked across channels) and gain computer (threshold, range, soft knee, attack).
- `EqEngine.h`: 24 bands in series with per-band stereo/left/right/mid/side routing, and the
  external sidechain buffer for dynamic bands that listen to it.
- `Character.h`: global Clean/Gentle/Warm saturation after the bands, run at 4x oversampling
  so its harmonics don't alias. Unity gain for quiet signals; Clean is a bit-exact bypass.
- `SpectralDynamics.h`: spectral dynamics: a 2048-point STFT (75% overlap, square-root Hann,
  exact rebuild) that gives every bin inside a spectral band its own gain offset. Runs
  after `EqEngine`; adds 2048 samples of latency while any band uses it.
- `CurveFit.h`: fits bells and shelves to a target curve (EQ Sketch, EQ Match).
- `SpectrumMatch.h`: long-term average spectrum and the match curve between two of them.
- `OutputStage.h`: smoothed output gain and bypass.
- `AudioTap.h`: lock-free mono feed from the audio thread to the analyzer.
- `SpectrumAnalyzer.h`: FFT, windowing and smoothing for the analyzer (runs on the message thread).
- `PeakMeter.h`: per-channel peak capture for the output meter.
- `BandSolo.h`: intelligent solo, auditions the region a band works on.
- `EditorLink.h`: the taps, meter, sidechain flag and solo band shared with the editor.
- `PhaseModes.h`: Natural and Linear Phase. Designs one FIR kernel for all static bands from the
  analog prototypes (a 2 x 2 matrix of kernels when mid/side bands mix left and right), runs it with
  uniformly partitioned FFT convolution, designs new kernels on a background thread (inline when
  rendering offline) and crossfades them in, and delays the sidechain to match the latency.
