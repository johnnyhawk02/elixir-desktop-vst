# Elixir Desktop (VST3)

The laptop drum machine at ~/Developer/elixir-desktop (johnnyhawk02/elixir-desktop) as a plugin, made from a copy of the
Elixir Mobile VST3 (~/Developer/Plugins/plugins/ElixirMobile). Nothing here changes the app, Elixir Mobile or the
Plugins workspace; their files are only read. GPL-3.0, like the LXR firmware it comes from. Built with JUCE (AGPL-3.0 edition) and clap-juce-extensions.

**Download it:** see [docs/DOWNLOADS.md](docs/DOWNLOADS.md). Help: [docs/help.html](docs/help.html). `docs/website-page.html` is the two together, ready to paste into a web page.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release          # add -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/Developer/Plugins/JUCE to skip the download,
                                                   # -DELIXIR_INSTALL=ON to copy VST3 + AU into the Mac's plug-in folders
node tools/build-web.mjs                           # after any change to the app: rebuilds web/index.html and the help
cmake --build build --target ElixirDesktop_All -j8
```

Formats: VST3, CLAP and Standalone everywhere, AU on Mac, LV2 on Linux. Windows needs the WebView2 NuGet package and
Linux the WebKitGTK headers (see `.github/workflows/release.yml`).

## Release

Bump the version in CMakeLists.txt (twice) and plugin.json, then push a tag: `git tag v1.2.0 && git push origin main v1.2.0`.
GitHub Actions builds every platform and publishes the Release; the download links always point at the newest one.

## How it works

- **Sound**: `Source/Lxr.*` is lxr.js (the LXR-02 engine, plus the firmware's never-shipped reverb, `ReverbBus`) and
  `Source/Player.*` is app/player.js (sequencer, rush, echo + reverb sends, sidechain/spacer, master distortion,
  compressor, resampling), ported line for line. `tools/compare.mjs` runs the app's own JS and the C++ on random
  message scripts and checks the output is the same sample for sample:
  `cmake --build build --target ElixirDesktop_Compare && node tools/compare.mjs 40`.
  Tables come from the app by `node tools/gen-tables.mjs` (writes Source/LxrTables.h).
  Density, mutes and send mutes are done by the page before it messages the player, so they need no C++.
  A change to the app's player.js or lxr.js needs porting here; compare.mjs shows where.
- **UI**: the app's page itself in a web view, laptop layout (window 1280x780, resizable from 1000 wide).
  `tools/build-web.mjs` builds it with the app's build.mjs and adds `web/boot.js` (localStorage = the plugin's settings
  file ~/Library/Application Support/Johnny/Elixir Desktop/storage.json on a Mac, shared by every instance) and `web/shim.js`
  (player messages go to the C++; step/level events, host play/stop and MIDI CCs come back). `web/plugin.css` hides the
  record buttons (the DAW records).
- **Host**: follows the DAW's play/stop and tempo, locked to its bar position; with the DAW stopped the page's own play
  button runs it at the page's tempo. MIDI notes 36-42 (C1-F#1) play drum 1, 2, 3, snare, cymbal, closed hat, open hat;
  MIDI CCs reach the page (perf knobs CC 1-12, seq bars CC 21-24 by default).
- **Parameters**: Perf 1..12 are the twelve perf knobs (0..1 across each knob's range), both ways, with the window
  closed too (the page runs for as long as the plugin does).
- **Saved with the project**: the page's snapshot of the sound and the last player message of each kind (replayed on
  load). Errors from the page go to ~/Library/Logs/Johnny/Elixir Desktop.log.
