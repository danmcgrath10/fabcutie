# FabCutie

An open-source parametric EQ plugin for macOS and Windows (AU, VST3 and Standalone), built with [JUCE](https://juce.com) and CMake.

FabCutie is an original project inspired by the workflow of modern "draw-on-the-graph" EQs such as FabFilter Pro-Q 3. It contains no FabFilter code, artwork or assets and is not affiliated with FabFilter.

> **Status:** the EQ engine and the draw-on-the-graph interface work. The real-time spectrum analyzer is next.

## What it does

- Up to 24 bands, each with: Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch, Band Pass or Tilt Shelf.
- Cut slopes of 6, 12, 18, 24, 30, 36, 48, 72 and 96 dB/oct. For cuts, Q 1 is a flat (Butterworth) knee and higher Q adds resonance.
- Per-band placement: Stereo, Left, Right, Mid or Side.
- Frequency 10 Hz to 30 kHz, gain ±30 dB, Q 0.025 to 40.
- Every control is automatable. Frequency, gain and Q glide smoothly; switching a band's type, slope, placement or on/off (and the plugin bypass) fades rather than clicks.
- Dynamic EQ on bell, shelf and tilt bands: switch on **DYN** in the band panel and the band's gain moves with the signal level. Above the threshold each dB over moves the gain a dB towards the range (a negative range ducks, a positive one lifts), with a 6 dB soft knee and attack and release times. The live gain change shows in the panel header.
- Sidechain per dynamic band: listen to the band's own input or the plugin's external sidechain input, filtered to the band's region (band pass for bells, low or high pass for shelves) or wide open.
- Zero latency. Filters are trapezoidal state variable filters, which stay stable and quiet under fast automation.

## Using the graph

| Action | What it does |
|---|---|
| Double-click empty space | Add a band there (a cut below 20 Hz or above 20 kHz, otherwise a bell) |
| Drag a node | Frequency and gain; for cut, notch and band pass, up/down sets Q. Hold Shift for fine moves |
| Scroll over a node | Q. With Alt/Option held, steps a cut's slope |
| Double-click a node | Remove the band |
| Cmd/Ctrl/Shift-click, or drag on empty space | Select several nodes, then drag them together |
| Right-click a node | Filter type, slope, placement, remove |
| Right-click empty space | Add a band, display range, remove all |
| Delete / Backspace | Remove the selected bands |
| `±12 dB` button (top right) | Cycle the display range: ±3, ±6, ±12, ±30 dB |

The selected band's panel floats over the graph: type, slope, placement and frequency/gain/Q knobs (double-click a knob to reset it), with the dynamics controls on a second row.

### External sidechain in Logic Pro

FabCutie has a stereo sidechain input. In Logic, choose a track or bus from the **Side Chain** menu in the plugin window's header, then set a dynamic band's source to **External**. Bands left on **Internal** keep listening to their own input. With no sidechain selected, external bands hear silence and stay at their static gain. Bands on Left, Right, Mid or Side get their own dashed curve, labelled L, R, M or S. The window is resizable and remembers its size and display range per instance.

## Roadmap

1. **Plugin skeleton** (done): JUCE/CMake project, AU/VST3/Standalone, CI with `auval`.
2. **EQ engine** (done): up to 24 bands of bell, shelf, cut, notch and tilt filters, slopes up to 96 dB/oct, stereo or mid/side per band, smoothed parameters.
3. **Interface:** interactive frequency graph with draggable nodes and a floating band panel (done); real-time spectrum analyzer behind the curve, then polish.
4. **Advanced:** dynamic EQ per band with external sidechain (done), linear-phase mode, analyzer collision display.

## Download a build

Every push builds on GitHub Actions. Open the latest run under **Actions → Build**, then download **FabCutie-macOS** (or **FabCutie-Windows**) from the run's Artifacts section.

## Build from source (macOS)

Requirements: Xcode (or the Command Line Tools) and CMake 3.22 or newer (`brew install cmake`).

```sh
git clone https://github.com/danmcgrath10/fabcutie.git
cd fabcutie
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

JUCE is downloaded automatically on the first configure. Run the DSP tests with `ctest --test-dir build -C Release --output-on-failure`. To use a JUCE checkout you already have, add `-DFABCUTIE_JUCE_PATH=/path/to/JUCE`.

Builds are universal (Apple Silicon and Intel). Outputs land in `build/FabCutie_artefacts/Release/`:

| Format     | Path                              |
|------------|-----------------------------------|
| AU         | `AU/FabCutie.component`           |
| VST3       | `VST3/FabCutie.vst3`              |
| Standalone | `Standalone/FabCutie.app`         |

For an Xcode project instead, use `cmake -S . -B build-xcode -G Xcode` and open `build-xcode/FabCutie.xcodeproj`.

## Install in Logic Pro

1. Copy the Audio Unit into your user Components folder:

   ```sh
   mkdir -p ~/Library/Audio/Plug-Ins/Components
   cp -R build/FabCutie_artefacts/Release/AU/FabCutie.component ~/Library/Audio/Plug-Ins/Components/
   ```

   Or configure with `-DFABCUTIE_COPY_AFTER_BUILD=ON` and every build installs itself.

2. If you downloaded the plugin from GitHub instead of building it, clear the quarantine flag and ad-hoc sign it so macOS will load it:

   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/FabCutie.component
   codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/Components/FabCutie.component
   ```

3. Make macOS rescan Audio Units, then check it validates:

   ```sh
   killall -9 AudioComponentRegistrar 2>/dev/null || true
   auval -v aufx Fceq Fbct
   ```

   The last line should read `AU VALIDATION SUCCEEDED`.

4. Open Logic Pro. If FabCutie is missing, open **Logic Pro → Settings → Plug-in Manager**, select FabCutie and click **Reset & Rescan Selection**. It appears under **Audio FX → FabCutie → FabCutie**.

## Install the VST3 (other DAWs)

Copy `FabCutie.vst3` to `~/Library/Audio/Plug-Ins/VST3/` on macOS or `C:\Program Files\Common Files\VST3\` on Windows.

## Project layout

```
CMakeLists.txt          JUCE fetch + plugin target
Source/
  PluginProcessor.*     audio processor, state save/restore
  PluginEditor.*        editor window (temporary band editor for now)
  Parameters.*          every automatable parameter, with stable IDs
  dsp/                  audio-thread code: EQ engine, filter design, output stage
  ui/                   editor components (frequency graph, analyzer)
Tests/                  offline EQ engine tests (ctest)
.github/workflows/      macOS + Windows CI, runs auval
```

## License

MIT, see [LICENSE](LICENSE). JUCE itself is used under its own license ([AGPLv3 or commercial](https://juce.com/legal/juce-8-licence/)); distributing binaries of an open-source plugin under the AGPL terms of JUCE is fine as long as the source stays available.
