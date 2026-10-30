# Elixir Desktop (VST3)

The laptop drum machine at ~/Developer/elixir-desktop (johnnyhawk02/elixir-desktop) as a plugin, made from a copy of the
Elixir Mobile VST3 (~/Developer/Plugins/plugins/ElixirMobile). Nothing here changes the app, Elixir Mobile or the
Plugins workspace; their files are only read. GPL-3.0, like the LXR firmware it comes from.

## Build

JUCE and `johnny_add_plugin()` come from ~/Developer/Plugins (read only). Everything builds into ./build.
```
cmake -B build -DCMAKE_BUILD_TYPE=Release          # once
node tools/build-web.mjs                           # after any change to the app: rebuilds web/index.html
cmake --build build --target ElixirDesktop_VST3 -j8 # installs ~/Library/Audio/Plug-Ins/VST3/Johnny/Elixir Desktop.vst3
```

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
  file ~/Library/Application Support/Johnny/Elixir Desktop/storage.json, shared by every instance) and `web/shim.js`
  (player messages go to the C++; step/level events, host play/stop and MIDI CCs come back). `web/plugin.css` hides the
  record buttons (the DAW records).
- **Host**: follows the DAW's play/stop and tempo, locked to its bar position; with the DAW stopped the page's own play
  button runs it at the page's tempo. MIDI notes 36-42 (C1-F#1) play drum 1, 2, 3, snare, cymbal, closed hat, open hat;
  MIDI CCs reach the page (perf knobs CC 1-12, seq bars CC 21-24 by default).
- **Parameters**: Perf 1..12 are the twelve perf knobs (0..1 across each knob's range), both ways, with the window
  closed too (the page runs for as long as the plugin does).
- **Saved with the project**: the page's snapshot of the sound and the last player message of each kind (replayed on
  load). Errors from the page go to ~/Library/Logs/Johnny/Elixir Desktop.log.
- `build/vst3render_artefacts/Release/vst3render ElixirDesktop --notes 0:36:1 [--bpm 120] [--state f]` renders the
  installed VST3 (`cmake --build build --target vst3render` first; WAVs land in proto/renders).
