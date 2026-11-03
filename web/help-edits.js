// Elixir Desktop plugin: the app's HELP, adjusted for the plugin (no recording, it installs as a plugin, the echo's newest
// controls). The page's help runs it on load (web/shim.js) and tools/build-web.mjs runs it for docs/help.html.
function elixirPluginHelp(H) {
  const tab = (id) => H.find((t) => t[0] === id);
  const edit = (id, pairs) => { const t = tab(id); for (const [from, to] of pairs) t[2] = t[2].replace(from, to); };
  edit('start', [
    ['then <b>play</b> and <b>rec</b>', 'then <b>play</b>'],
    [' <b>rec</b> records a WAV.', ''],
    [/<p>\(On a screen under 1000 px wide[\s\S]*?<\/p>/, ''],
  ]);
  edit('fx', [[/<li><b>echo<\/b>:[\s\S]*?<\/li>/, '<li><b>echo</b>: send, time, fine, tone, feedback and width. With <b>sync</b> ticked the time sits on musical lengths; unticked it runs free, 1 to 2000 milliseconds, on the same fader. <b>fine</b> tunes the time by up to 1% either way. Tick <b>ping pong</b> to bounce it left and right.</li>']]);
  edit('keys', [[' <b>enter</b> record, <b>l</b> record a loop.', '']]);
  edit('more', [
    [/<li>Install it as an app[\s\S]*?<\/li>/, ''],
    [/<li>The version number is by the e;[\s\S]*?<\/li>/, '<li>The version number is on the cauldron.</li>'],
  ]);
  H.splice(H.indexOf(tab('rec')), 1);
  H.splice(1, 0, ['daw', 'in your music software', `<ul><li>Load Elixir Desktop on an instrument track. It follows your song's tempo and its play and stop, locked to the bars. With the song stopped, its own <b>play</b> button runs it at its own tempo.</li>
<li>MIDI notes C1 to F#1 (36 to 42) play drum 1, 2, 3, snare, cymbal, closed hat and open hat.</li>
<li>MIDI CC 1 to 12 turn the knobs and CC 21 to 24 set the step sequencer shown; any channel. Choose other CCs in <b>options</b> › <b>user settings</b> › <b>midi</b>.</li>
<li>The twelve knobs are the automation parameters Perf 1 to 12. Automate them, and moving a knob writes back. If you reassign a knob, its parameter follows.</li>
<li>Your song saves the whole sound. Your own presets live with the plugin, shared by every copy of it. To bring presets from the web version, use <b>export presets</b> there and <b>import</b> here.</li>
<li>To record it, record its track in your music software.</li></ul>`]);
  return H;
}
