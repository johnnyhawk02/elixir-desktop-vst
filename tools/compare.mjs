// Runs Elixir Desktop's own JS player (app/player.js + the lxr engine, read from ~/Developer/elixir-desktop) and the
// C++ twin (ElixirDesktop_Compare) on the same random message scripts, and compares their output sample for sample.
//   node tools/compare.mjs [scenarios=20] [seed=1] [--keep]
// Build the C++ side first: cmake --build build --target ElixirDesktop_Compare
import { readFileSync, writeFileSync, readdirSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { homedir, tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';

const APP = process.env.ELIXIR_APP || join(homedir(), 'Developer/elixir-desktop');
const BIN = fileURLToPath(new URL('../build/ElixirDesktop_Compare_artefacts/Release/ElixirDesktop_Compare' + (process.platform === 'win32' ? '.exe' : ''), import.meta.url));
const args = process.argv.slice(2).filter((a) => !a.startsWith('--'));
const nScen = Number(args[0] || 20), seed0 = Number(args[1] || 1);

// the engine and player as one classic script, the way build.mjs bundles them for the page
const read = (f) => readFileSync(join(APP, f), 'utf8').replace(/\r\n/g, '\n'); // (a Windows checkout has CRLF)
const strip = (src) => src.replace(/^import[\s\S]*?from\s+'[^']+';\n/gm, '').replace(/^export \{[^}]*\};\n/gm, '').replace(/^export (const|function|class) /gm, '$1 ');
const engine = ['lxr-tables.js', 'lxr-ui-data.js', 'lxr-clicks.js', 'lxr.js'].map((f) => strip(read(f))).join('\n');
const names = ['LxrEngine', 'PAR', 'BLOCK', 'REAL_FS', 'MIXER_ROUTING_FX_BUS', 'FX_DISTORTION', 'FX_RING', 'FX_DELAY', 'FxBus', 'Rng', 'ReverbBus'];
const Player = new Function(`const LXR = (() => {\n${engine}\nreturn { ${names.join(', ')} };\n})();\n${read('app/player.js')}\nreturn Player;`)();
const PAR = new Function(`${strip(read('lxr-tables.js'))}\nreturn PAR;`)();

const kitFiles = readdirSync(join(APP, 'kits'), { recursive: true }).filter((f) => /\.snd$/i.test(f)).sort();
const loadKit = (f) => Array.from(readFileSync(join(APP, 'kits', f)).subarray(8, 8 + PAR.END_OF_SOUND_PARAMETERS));

function scenario(seed) {
  let s = seed >>> 0 || 1;
  const rnd = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
  const ri = (n) => Math.floor(rnd() * n), pick = (a) => a[ri(a.length)], chance = (p) => rnd() < p;
  const kit = loadKit(pick(kitFiles));
  if (chance(0.7)) { kit[PAR.FX_TYPE] = 4; kit[PAR.FX_P3] = 90; kit[PAR.FX_P4] = 50; } else kit[PAR.FX_TYPE] = ri(5);
  for (let n = 0; n < 6; n++) if (chance(0.5)) kit[PAR.AUDIO_OUT1 + n] = 6;
  const page = () => { const P = { on: [], acc: [], bar: [] }; for (let t = 0; t < 7; t++) {
    P.on.push(Array.from({ length: 16 }, () => (chance(t < 5 ? 0.25 : 0.5) ? 1 : 0)));
    P.acc.push(Array.from({ length: 16 }, () => (chance(0.2) ? 1 : 0)));
    P.bar.push(Array.from({ length: 16 }, () => (chance(0.1) ? pick([2, 4, -2, 102]) : 0))); } return P; };
  const seq = (playing) => {
    const P = page(); if (chance(0.3)) P.pages = Array.from({ length: 1 + ri(4) }, page);
    return { type: 'seq', pattern: P, bpm: 70 + ri(110), vel: 60 + ri(68), patternOn: !chance(0.05), accentDb: ri(12), playing,
      groove: { off: Array.from({ length: 7 }, () => Array.from({ length: 16 }, () => (chance(0.5) ? (rnd() - 0.5) * 0.6 : 0))) } };
  };
  const mix = () => ({ type: 'mix', master: { comp: chance(0.8), thresh: -30 + ri(30), ratio: 1 + ri(8), attack: 1 + ri(30), release: 20 + ri(300),
      makeup: ri(8), duck: rnd() * 0.8, duckRelease: 40 + ri(300), duckFx: chance(0.3), duckOn: chance(0.7), space: chance(0.4) ? ri(101) : 0 },
    fx: Array.from({ length: 6 }, () => (chance(0.5) ? rnd() : 0)), echo: { pct: ri(201), sync: !chance(0.4), free: rnd(), fine: chance(0.5) ? ri(201) - 100 : 0 },
    dist: { type: ri(3), p: [ri(128), ri(128), ri(128), ri(128)], mix: chance(0.4) ? ri(101) : 0 }, verb: { time: ri(128), decay: ri(128), tone: ri(128), early: ri(128) },
    sendD: Array.from({ length: 6 }, () => 0), sendR: Array.from({ length: 6 }, () => (chance(0.3) ? rnd() : 0)) });
  const first = [{ type: 'kit', values: kit }, mix(), { type: 'delay', pos: rnd() }, { type: 'rushSet', set: {} }];
  if (chance(0.3)) first.push({ type: 'xmod', amt: Array.from({ length: 36 }, () => (chance(0.15) ? rnd() * 2 - 1 : 0)), target: Array.from({ length: 6 }, () => pick(['fm', 'filter', 'ring'])) });
  if (chance(0.4)) first.push({ type: 'rush', v: pick([0, 50, 77, 85, 92, 99]), t: ri(7) });
  first.push(seq(true));
  const blocks = [{ msgs: first, frames: 1 + ri(1024) }];
  const total = 48000 * (3 + ri(5));
  let done = blocks[0].frames;
  while (done < total) {
    const msgs = [];
    if (chance(0.05)) msgs.push({ type: 'param', par: 1 + ri(PAR.END_OF_SOUND_PARAMETERS - 2), value: ri(128) });
    if (chance(0.02)) msgs.push({ type: 'trig', voice: ri(7), vel: 1 + ri(127) });
    if (chance(0.01)) msgs.push({ type: 'mute', muted: Array.from({ length: 6 }, () => chance(0.2)) });
    if (chance(0.01)) msgs.push({ type: 'rush', v: ri(101) });
    if (chance(0.01)) msgs.push(mix());
    if (chance(0.005)) msgs.push({ type: 'atBar', m: seq(true) });
    if (chance(0.005)) msgs.push({ type: 'atBar', m: { type: 'kit', values: loadKit(pick(kitFiles)) } });
    if (chance(0.003)) msgs.push(seq(chance(0.5)));
    const frames = 1 + ri(1024);
    blocks.push({ msgs, frames }); done += frames;
  }
  if (process.env.NOCOMP) for (const b of blocks) for (const m of b.msgs) if (m.type === "mix") m.master.comp = false;
  if (process.env.NOSPACE) for (const b of blocks) for (const m of b.msgs) if (m.type === "mix") m.master.space = 0;
  return { rate: pick([44100, 48000, 96000]), blocks };
}

const dir = mkdtempSync(join(tmpdir(), 'elixir-cmp-'));
let bad = 0;
for (let k = 0; k < nScen; k++) {
  const sc = scenario(seed0 * 1000 + k);
  const p = new Player(sc.rate), outs = [];
  for (const b of sc.blocks) {
    for (const m of b.msgs) p.msg(JSON.parse(JSON.stringify(m)));
    const L = new Float32Array(b.frames), R = new Float32Array(b.frames);
    p.process(L, R); p.events.length = 0;
    const o = new Float32Array(b.frames * 2); for (let i = 0; i < b.frames; i++) { o[2 * i] = L[i]; o[2 * i + 1] = R[i]; }
    outs.push(o);
  }
  const js = new Float32Array(outs.reduce((n, o) => n + o.length, 0)); let at = 0; for (const o of outs) { js.set(o, at); at += o.length; }
  const sf = join(dir, `s${k}.json`), of = join(dir, `s${k}.f32`);
  writeFileSync(sf, JSON.stringify(sc));
  execFileSync(BIN, [sf, of]);
  const buf = readFileSync(of), cpp = new Float32Array(buf.buffer, buf.byteOffset, buf.length / 4);
  // the C++ writes big-endian floats (juce writeFloat is little-endian on this Mac; check length only)
  let maxd = 0, firstBad = -1, peak = 0;
  for (let i = 0; i < js.length; i++) { const d = Math.abs(js[i] - cpp[i]); if (d > maxd) maxd = d; if (d > 1e-6 && firstBad < 0) firstBad = i; peak = Math.max(peak, Math.abs(js[i])); }
  const ok = cpp.length === js.length && maxd <= 1e-6;
  if (!ok) bad++;
  console.log(`${ok ? 'ok ' : 'BAD'} #${k} rate ${sc.rate} ${(js.length / 2 / sc.rate).toFixed(1)} s peak ${peak.toFixed(3)} max diff ${maxd.toExponential(2)}${firstBad >= 0 ? ` first at frame ${firstBad >> 1} (${((firstBad >> 1) / sc.rate).toFixed(3)} s)` : ''}${cpp.length !== js.length ? ` LENGTH ${cpp.length} vs ${js.length}` : ''}`);
}
console.log(bad ? `${bad} of ${nScen} differ` : `all ${nScen} match`);
if (!process.argv.includes('--keep')) rmSync(dir, { recursive: true, force: true }); else console.log(dir);
