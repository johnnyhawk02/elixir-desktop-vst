// Elixir Desktop VST3, after the page's code: the player runs in the plugin (C++), not in an AudioWorklet.
// Messages the page posts to the player go to the plugin; its step and level events come back the same shape.
// The page also hands the plugin a snapshot of the whole sound now and then (the same fields as a preset), which
// the plugin saves with the DAW's project and gives back when the page opens again.
(() => {
  const N = typeof ELIXIR_NATIVE === 'object' ? ELIXIR_NATIVE : {};
  const emit = window.elixirEmit;
  let snapT = 0;
  const snapshot = () => ({ name: kitSel.selectedOptions[0] ? kitSel.selectedOptions[0].textContent : '', kit: Array.from(values),
    xm: { amt: xm.amt.slice(), target: xm.target.slice() }, fx: mix.fx.slice(), fx3: fx3Snapshot(), dpos: delayPos, song: songSnapshot() });
  // (taking one puts sequenced faders back to their settings and then forward again, which sends messages: those
  // don't ask for another, or a running fader sequencer would keep it going round)
  let snapping = false;
  const snap = () => {
    if (snapping) return;
    clearTimeout(snapT);
    snapT = setTimeout(() => { snapping = true; try { emit({ t: 'ui', ui: snapshot() }); } catch (e) { console.error(e); } finally { snapping = false; } }, 300);
  };

  ctx = { state: 'running', outputLatency: 0, sampleRate: 48000, resume: async () => {} };
  fallback = { msg: (m) => { emit({ t: 'msg', m }); snap(); } };
  startAudio = async () => {};
  // exports go to a save dialog in the plugin, copies to the Mac's clipboard
  mOffer = async (t) => { emit({ t: 'save', name: t.name, text: await t.blob.text() }); return true; };
  try { Object.defineProperty(navigator, 'clipboard', { value: { writeText: async (s) => emit({ t: 'copy', s: String(s) }), readText: async () => '' }, configurable: true }); } catch (e) { /* the browser's own */ }

  // the sound: the one this plugin was last showing (or saved in the project), else the page's own start
  if (N.ui && N.ui.kit) {
    pending.length = 0;
    try { const k = fromStored(N.ui); useKit(k); if (k.song) useSong(k.song, false); setDirty(false); }
    catch (e) { console.error(e); }
  } else {
    fallback.msg({ type: 'kit', values: effValues() });
    sendSeq();
    while (pending.length) send(pending.shift());
  }
  if (N.playing && !playing) $('play').click();

  // the PERF faders are the plugin's parameters Perf 1..12 (0..1 across each fader's range): the plugin moves them
  // (the DAW's automation), and their positions and readouts go back to it whenever they change
  const slotFd = (i) => { const s = mSlotEls[i]; return s && mFaders.find((f) => f.el === s.el); };
  const slotFrac = (fd) => { const hi = fd.hiOf(), v = fd.base !== null ? fd.base : fd.get(); return hi > fd.min ? (v - fd.min) / (hi - fd.min) : 0; };
  let sent = [];
  const fromHost = []; // [i]: { x, f }: the DAW set x and the fader snapped to f; while it stays at f, it reports x (no tug-of-war with automation)
  const reportSlots = () => {
    const v = [], show = [];
    for (let i = 0; i < SLOT0.length; i++) { const fd = slotFd(i); const f = fd ? slotFrac(fd) : 0, h = fromHost[i]; v.push(h && Math.abs(h.f - f) < 1e-9 ? h.x : f);
      show.push(fd ? `${fd.label} ${fd.show(fd.base !== null ? fd.base : fd.get())}`.replace(/\s+/g, ' ') : ''); }
    if (v.some((x, i) => Math.abs(x - (sent[i] ?? -1)) > 1e-4) || show.join() !== sent.show) { sent = v; sent.show = show.join(); emit({ t: 'slots', v, show }); }
  };
  setInterval(reportSlots, 100);
  let lastHost = null;
  const setSlot = (i, x) => {
    lastHost = { i, x };
    const fd = slotFd(i); if (!fd || !Number.isFinite(x)) return;
    const v = fd.snap ? fd.snap(fd.min + x * (fd.hiOf() - fd.min)) : fd.min + x * (fd.hiOf() - fd.min);
    if (fd.base !== null) { fd.base = v; mSeqApply(fd); } else fd.put(v);
    fromHost[i] = { x, f: slotFrac(fd) }; sent[i] = x; // (not sent back)
  };

  // a fader that has lost its number (NaN): logged with how it got there (~/Library/Logs/Johnny/Elixir Desktop.log) and put back
  setInterval(() => {
    for (const f of mFaders) {
      let v; try { v = f.get(); } catch (e) { continue; }
      if (Number.isFinite(v) && (f.base === null || Number.isFinite(f.base)) && Number.isFinite(f.off)) continue;
      console.error('fader NaN', f.key, JSON.stringify({ v, base: f.base, off: f.off, seq: f.seq, glide: f.glide, lastHost, tempo: perf.tempo }));
      f.glide = null; f.off = 0;
      const d = typeof f.dflt === 'function' ? f.dflt() : f.dflt, to = Number.isFinite(f.base) ? f.base : d;
      f.base = null;
      if (Number.isFinite(to)) f.put(to);
    }
  }, 500);

  // from the plugin: the player's step and level events, the host's transport, MIDI CCs (the PERF faders' CCs)
  window.__JUCE__.backend.addEventListener('elixirEv', (batch) => {
    for (const ev of batch) {
      if (ev.type === 'host') { if (!!ev.playing !== playing) $('play').click(); }
      else if (ev.type === 'slot') setSlot(ev.i, ev.x);
      else if (ev.type === 'midi') { try { onMidi({ data: ev.d }); } catch (e) { /* no MIDI map */ } }
      else onEvent(ev);
    }
  });
  emit({ t: 'ready' });
})();
