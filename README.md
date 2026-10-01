# FabCutie

An open-source parametric EQ plugin for macOS and Windows (AU, VST3 and Standalone), built with [JUCE](https://juce.com) and CMake.

FabCutie is an original project inspired by the workflow of modern "draw-on-the-graph" EQs such as FabFilter Pro-Q 3. It contains no FabFilter code, artwork or assets and is not affiliated with FabFilter.

> **Status:** early skeleton. Right now FabCutie is a clean pass-through with an output gain and bypass, so the build, AU validation and DAW loading can be verified before the EQ lands.

## Roadmap

1. **Plugin skeleton** (this): JUCE/CMake project, AU/VST3/Standalone, CI with `auval`.
2. **EQ engine:** up to 24 bands of bell, shelf, cut, notch and tilt filters, slopes up to 96 dB/oct, stereo or mid/side per band, smoothed parameters.
3. **Interface:** interactive frequency graph (drag nodes for frequency and gain, scroll for Q) over a real-time spectrum analyzer.
4. **Advanced:** dynamic EQ per band, linear-phase mode, analyzer collision display.

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

JUCE is downloaded automatically on the first configure. To use a JUCE checkout you already have, add `-DFABCUTIE_JUCE_PATH=/path/to/JUCE`.

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
  PluginEditor.*        editor window (placeholder UI for now)
  Parameters.*          every automatable parameter, with stable IDs
  dsp/                  audio-thread code (output stage now, EQ engine next)
  ui/                   editor components (frequency graph, analyzer)
.github/workflows/      macOS + Windows CI, runs auval
```

## License

MIT, see [LICENSE](LICENSE). JUCE itself is used under its own license ([AGPLv3 or commercial](https://juce.com/legal/juce-8-licence/)); distributing binaries of an open-source plugin under the AGPL terms of JUCE is fine as long as the source stays available.
