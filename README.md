# FabCutie

An open-source parametric EQ plugin for macOS and Windows (AU, VST3, CLAP and Standalone), built with [JUCE](https://juce.com) and CMake.

FabCutie is an original project inspired by the workflow of modern "draw-on-the-graph" EQs such as FabFilter Pro-Q 3. It contains no FabFilter code, artwork or assets and is not affiliated with FabFilter.

> **Status:** the EQ engine, the draw-on-the-graph interface, the real-time spectrum analyzer and the workflow features (undo, A/B, presets, MIDI learn) work.

## What it does

- Up to 24 bands, each with: Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch, Band Pass or Tilt Shelf.
- Cut slopes of 6, 12, 18, 24, 30, 36, 48, 72 and 96 dB/oct. For cuts, Q 1 is a flat (Butterworth) knee and higher Q adds resonance.
- Per-band placement: Stereo, Left, Right, Mid or Side.
- Mono, stereo and surround up to 9.1.6 (16 channels), including 5.1, 7.1, 7.1.4 (Dolby Atmos beds) and 9.1.6.
- Instance list: see every FabCutie in the session, overlay their curves on this one, and edit any of them from this window.
- Frequency 10 Hz to 30 kHz, gain ±30 dB, Q 0.025 to 40.
- Every control is automatable. Frequency, gain and Q glide smoothly; switching a band's type, slope, placement or on/off (and the plugin bypass) fades rather than clicks.
- Dynamic EQ on bell, shelf and tilt bands: switch on **DYN** in the band panel and the band's gain moves with the signal level. Above the threshold each dB over moves the gain a dB towards the range (a negative range ducks, a positive one lifts), with a 6 dB soft knee and attack and release times. The live gain change shows in the panel header.
- Sidechain per dynamic band: listen to the band's own input or the plugin's external sidechain input, filtered to the band's region (band pass for bells, low or high pass for shelves) or wide open.
- Spectral dynamics: switch on **SPEC** next to **DYN** and the band works bin by bin, so only the frequencies inside it that cross the threshold move (one ringing resonance, not the whole band). The threshold reads like the analyzer: a full-scale sine is 0 dB. While any band is spectral the plugin adds 2048 samples of latency on top of the phase mode's.
- **EQ Sketch:** press **Sketch** in the header and draw the curve you want on the graph; on release it becomes up to 8 bells and shelves in the free band slots. Esc leaves sketch mode.
- **EQ Match:** press **Match**, pick a reference (the **Sidechain** input, or a **Captured** one: play the reference and press **Capture**), play your track with **Learn** on, then **Apply** to add up to 12 bands that move its tonal balance towards the reference. The level difference is ignored, **Amount** scales the result, and the captured reference is saved with the session.
- Three phase modes (see below): Zero Latency, Natural Phase and Linear Phase. In Zero Latency the filters are trapezoidal state variable filters, which stay stable and quiet under fast automation.
- Real-time spectrum analyzer behind the curves: input (Pre), output (Post) and sidechain spectra, with adjustable range, speed, tilt and resolution, and Freeze.
- Collision detection: red shading where the output and the sidechain signal crowd the same frequencies.
- Spectrum grab: hover a peak in the spectrum, click it and drag to cut (or boost) it right away.
- Intelligent band solo: hear only the part of the spectrum a band works on.
- Output level meter with peak hold and a resettable peak readout.
- Undo and redo, A/B compare, factory and user presets, copy and paste of bands, typed values, auto gain, gain scale, phase invert, window sizes and MIDI learn (see [Workflow](#workflow)).

## Using the graph

| Action | What it does |
|---|---|
| Double-click empty space | Add a band there (a cut below 20 Hz or above 20 kHz, otherwise a bell) |
| Drag a node | Frequency and gain; for cut, notch and band pass, up/down sets Q. Hold Shift for fine moves |
| Scroll over a node | Q. With Alt/Option held, steps a cut's slope |
| Double-click a node | Remove the band |
| Cmd/Ctrl/Shift-click, or drag on empty space | Select several nodes, then drag them together |
| Right-click a node | Filter type, slope, placement, enter values, copy, paste settings, remove |
| Right-click empty space | Add a band, display range, paste bands, remove all |
| Delete / Backspace | Remove the selected bands |
| `±12 dB` button (top right) | Cycle the display range: ±3, ±6, ±12, ±30 dB |
| Hover a spectrum peak, then click and drag | Spectrum grab: adds a bell on the peak and drags it |
| Alt/Option-click a node and hold | Solo the band while the mouse is down |
| Cmd/Ctrl-C, Cmd/Ctrl-V | Copy the selected bands; paste them as new bands (also between FabCutie instances) |
| Return, or right-click a node → Enter values... | Type the band's frequency, gain and Q |
| Cmd/Ctrl-Z, Cmd/Ctrl-Shift-Z | Undo, redo |

The selected band's panel floats over the graph: type, slope, placement, frequency/gain/Q knobs (double-click a knob to reset it) and a Solo button, with the dynamics controls on a second row. Bands on Left, Right, Mid or Side get their own dashed curve, labelled L, R, M or S. The window is resizable and remembers its size and display range per instance.

### External sidechain in Logic Pro

FabCutie has a stereo sidechain input. In Logic, choose a track or bus from the **Side Chain** menu in the plugin window's header, then set a dynamic band's source to **External**. Bands left on **Internal** keep listening to their own input. With no sidechain selected, external bands hear silence and stay at their static gain. The same input feeds the analyzer's sidechain spectrum.

## Instance list

The button next to the version number names the instance the window is showing (the host's track name, or "FabCutie 1", "FabCutie 2"...). Click it to list every FabCutie in the session:

- **Show** draws that instance's curve behind this one's, in its own colour, with a legend in the corner. Use it to carve space between tracks.
- **Edit** points this window at that instance: the graph, band panel, analyzer, meter, presets, undo, A/B, output bar, bypass, character and phase mode all control it, and a coloured frame says which one you are editing. **Back** returns to this window's own instance.
- Double-click a name to rename the instance (saved with the session). Clear the name to go back to the track name.

Instances find each other when the host loads them into the same process, as Logic Pro and most DAWs do. A host that runs every plug-in in its own sandboxed process shows each instance on its own.

## Surround

FabCutie runs on mono, stereo and every speaker layout up to 9.1.6 (16 channels), with the same layout in and out. In Logic Pro, insert it on a surround track or bus and it opens in that layout. In surround there is no mid/side matrix; a band's placement chooses speakers by where they sit, and the menus say so:

| Placement | Stereo | Surround |
|---|---|---|
| Stereo / All | both channels | every speaker |
| Left | left | the speakers on the left (L, Ls, Lrs, Ltf...) |
| Right | right | the speakers on the right |
| Mid / Centre | the mid signal | the centre line: C, LFE, Cs and the top/bottom centres |
| Side / Sides | the side signal | every speaker off the centre line |

The sidechain input stays mono or stereo. Surround always runs at Zero Latency, and spectral bands work as ordinary dynamic bands there, because the linear phase and spectral stages are stereo.

## Roadmap

1. **Plugin skeleton** (done): JUCE/CMake project, AU/VST3/Standalone, CI with `auval`.
2. **EQ engine** (done): up to 24 bands of bell, shelf, cut, notch and tilt filters, slopes up to 96 dB/oct, stereo or mid/side per band, smoothed parameters.
3. **Interface** (done): interactive frequency graph with draggable nodes and a floating band panel.
4. **Analyzer** (done): pre/post/sidechain spectrum, range, speed, tilt, resolution, freeze, spectrum grab, collision detection, band solo, output meter.
5. **Workflow** (done): undo/redo, A/B, presets, copy and paste of bands, value entry, auto gain, gain scale, phase invert, MIDI learn.
6. **Filter extras** (done): All Pass and Flat Tilt shapes, brickwall slope, piano roll.
7. **Dynamic EQ** with sidechain, character modes, phase modes (linear and natural phase), EQ Sketch/Match and spectral dynamics (all done).
8. **Session features** (done): instance list, CLAP format, surround up to 9.1.6.

## Workflow

The header holds **undo** and **redo**, the **preset browser** (arrows step through presets, the name opens the menu), **A / B** and **A>B**, and a **•••** menu.

- **Undo / redo** (Cmd/Ctrl-Z, Cmd/Ctrl-Shift-Z) covers every edit made in the plugin window: a whole drag is one step, as is an A/B switch or a preset load. Host automation is not recorded, and bypass is left out.
- **A/B:** two complete settings. Click **B** to switch (the first time it starts as a copy of A), **A>B** copies the current slot to the other. Both are saved with the session.
- **Presets:** a few factory presets, plus your own from **Save preset...**. User presets are files in `~/Library/Audio/Presets/FabCutie` on macOS (the app data folder `FabCutie/Presets` on Windows), so they are shared by every session and can be copied between machines. Presets older than a feature load with that feature at its default.
- **Copy and paste bands:** see the table above. Pasting settings onto a band replaces its type, frequency, gain, Q, slope, placement and dynamics.
- **Typing values:** click a knob's value in the band panel, or press Return on a selected node. Frequencies take Hz, `k` for kHz (`2.5k`) or a note name (`A4`, `C#2`).
- **Gain scale** (bottom right): scales the gain of every bell, shelf and tilt, and each dynamic band's range. 100 % leaves them as set, 0 % flattens the EQ, negative values invert it, up to 200 %. The graph shows the scaled curve.
- **Auto gain:** offsets the output by the average level change of the bells, shelves and tilts, worked out from the curve (not measured, so it never pumps). Cuts, notches and band passes are not compensated. The button shows the offset it applies.
- **Ø:** flips the output polarity.
- **Window size:** drag the corner, or pick Small, Medium, Large or Extra large from the **•••** menu. The size is saved with the session.
- **MIDI learn:** right-click any knob or menu in the window, choose **MIDI Learn** and move a controller. That CC (on any channel) then drives the control; right-click again to forget it, or clear them all from the **•••** menu. The assignments are saved with the session. In Logic Pro, Audio Unit effects do not receive MIDI, so use Logic's own controller assignments (Learn mode) there; MIDI learn works in the VST3 and Standalone versions.

## The analyzer

The strip under the graph switches the **Pre** (input), **Post** (output) and **Sidechain** spectra on and off, and **Freeze** holds the display. The **Analyzer** menu sets:

- **Range:** 60, 90 or 120 dB from the top of the graph to the bottom.
- **Speed:** how quickly the spectrum falls back, from Very slow to Very fast.
- **Tilt:** 0 to 6 dB/oct around 1 kHz. At 4.5 dB/oct pink noise, and most mixes, look level.
- **Resolution:** 2048 to 16384-point FFT. Higher shows more low-end detail but reacts more slowly.
- **Show collisions with sidechain** and **Spectrum grab**.

These settings are saved with the session. To feed the sidechain in Logic Pro, pick a track or bus in the **Side Chain** menu at the top of the plugin window.

## Phase modes

The phase menu in the bar under the graph (next to the character menu) picks how the bands are realised:

- **Zero Latency** (default): minimum-phase IIR filters, no latency. Like every digital EQ of this kind, curves near Nyquist narrow slightly ("cramping").
- **Natural Phase**: an FIR filter built from the analog prototypes' magnitude and phase, so the EQ behaves like an analog one all the way up to Nyquist, with no cramping. Latency is about 7 ms (320 samples at 44.1/48 kHz).
- **Linear Phase**: an FIR filter with the analog magnitude and no phase shift, so boosts and cuts don't smear transients or shift the relative timing of the low end. It pre-rings instead, most on steep cuts and narrow low bells. Pick the resolution in the same menu: Low, Medium, High, Very High or Maximum use 2048 to 32768-tap kernels (at 44.1/48 kHz, scaled up at higher rates). Higher resolution is more accurate in the low end; latency is half the kernel, from about 27 ms (Low) to 347 ms (Maximum) at 48 kHz.

The plugin reports its latency to the host, so Logic Pro and other DAWs keep it in time with the other tracks. Changing mode fades the output out and back in. Dynamic bands (other than spectral ones) always run as zero-latency IIR filters after the FIR, since their gain moves faster than a kernel could be redesigned; everything else (all band types, slopes and stereo/left/right/mid/side placement) works in every mode. The phase mode isn't automatable, because it changes the latency.

## Download and install

Get the installer from the [latest release](https://github.com/danmcgrath10/fabcutie/releases/latest) (no GitHub account needed):

- **macOS:** `FabCutie-<version>-macOS.pkg`. Double-click it and pick the formats you want: Audio Unit (Logic Pro, GarageBand), VST3, CLAP and the standalone app. The plug-ins go into `/Library/Audio/Plug-Ins`, and any FabCutie you copied into your own Plug-Ins folder by hand is replaced so your DAW only sees one. Then quit and reopen Logic Pro.

  The installer isn't signed with an Apple Developer ID, so the first time macOS says it can't check it for malware. Click **Done**, open **System Settings → Privacy & Security**, scroll down to the message about FabCutie and click **Open Anyway**. You only need to do this once per download.
- **Windows:** `FabCutie-<version>-Windows-Setup.exe` installs the VST3 and CLAP into `C:\Program Files\Common Files` and the standalone app into `C:\Program Files\FabCutie`. If SmartScreen warns about an unknown publisher, click **More info → Run anyway**. Uninstall it from **Settings → Apps**.

If FabCutie doesn't show up in Logic Pro, open **Logic Pro → Settings → Plug-in Manager**, select FabCutie and click **Reset & Rescan Selection**. It appears under **Audio FX → FabCutie → FabCutie**.

Every push to `main` also builds on GitHub Actions: the run's Artifacts section has the same installers (**FabCutie-macOS-Installer**, **FabCutie-Windows-Installer**) and plain zips of each format. To publish a release, set the version in `CMakeLists.txt`, then either push a matching tag (`git tag v0.5.0 && git push origin v0.5.0`) or open **Actions → Build → Run workflow** on `main` and tick **publish**.

## Build from source (macOS)

Requirements: Xcode (or the Command Line Tools) and CMake 3.22 or newer (`brew install cmake`).

```sh
git clone https://github.com/danmcgrath10/fabcutie.git
cd fabcutie
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

JUCE and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) (for the CLAP build) are downloaded automatically on the first configure; add `-DFABCUTIE_BUILD_CLAP=OFF` to skip CLAP. Run the DSP tests with `ctest --test-dir build -C Release --output-on-failure`. To use a JUCE checkout you already have, add `-DFABCUTIE_JUCE_PATH=/path/to/JUCE`.

Builds are universal (Apple Silicon and Intel). Outputs land in `build/FabCutie_artefacts/Release/`:

| Format     | Path                              |
|------------|-----------------------------------|
| AU         | `AU/FabCutie.component`           |
| VST3       | `VST3/FabCutie.vst3`              |
| CLAP       | `CLAP/FabCutie.clap`              |
| Standalone | `Standalone/FabCutie.app`         |

For an Xcode project instead, use `cmake -S . -B build-xcode -G Xcode` and open `build-xcode/FabCutie.xcodeproj`.

## Install a build from source in Logic Pro

1. Copy the Audio Unit into your user Components folder:

   ```sh
   mkdir -p ~/Library/Audio/Plug-Ins/Components
   cp -R build/FabCutie_artefacts/Release/AU/FabCutie.component ~/Library/Audio/Plug-Ins/Components/
   ```

   Or configure with `-DFABCUTIE_COPY_AFTER_BUILD=ON` and every build installs itself.

   If you installed FabCutie with the installer before, delete `/Library/Audio/Plug-Ins/Components/FabCutie.component` first so Logic doesn't load that copy instead.

2. Make macOS rescan Audio Units, then check it validates:

   ```sh
   killall -9 AudioComponentRegistrar 2>/dev/null || true
   auval -v aufx Fceq Fbct
   ```

   The last line should read `AU VALIDATION SUCCEEDED`.

3. Open Logic Pro. If FabCutie is missing, open **Logic Pro → Settings → Plug-in Manager**, select FabCutie and click **Reset & Rescan Selection**. It appears under **Audio FX → FabCutie → FabCutie**.

## Install a VST3 or CLAP build from source (other DAWs)

Copy `FabCutie.vst3` to `~/Library/Audio/Plug-Ins/VST3/` on macOS or `C:\Program Files\Common Files\VST3\` on Windows.

For CLAP hosts (Bitwig, REAPER and others), copy `FabCutie.clap` to `~/Library/Audio/Plug-Ins/CLAP/` on macOS or `C:\Program Files\Common Files\CLAP\` on Windows.

## Project layout

```
CMakeLists.txt          JUCE fetch + plugin target
Source/
  PluginProcessor.*     audio processor, state save/restore
  PluginEditor.*        editor window
  InstanceRegistry.h    every FabCutie in the host process, for the instance list
  Parameters.*          every automatable parameter, with stable IDs
  dsp/                  audio-thread code: EQ engine, filter design, output stage
  ui/                   editor components (frequency graph, analyzer, meter)
Tests/                  offline EQ engine tests (ctest)
packaging/              macOS .pkg and Windows (Inno Setup) installers
.github/workflows/      macOS + Windows CI: tests, installers, auval; releases on version tags
```

## License

MIT, see [LICENSE](LICENSE). JUCE itself is used under its own license ([AGPLv3 or commercial](https://juce.com/legal/juce-8-licence/)); distributing binaries of an open-source plugin under the AGPL terms of JUCE is fine as long as the source stays available.
