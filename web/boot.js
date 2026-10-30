// Elixir Desktop VST3, top of <head>: the page's localStorage is the plugin's settings file
// (~/Library/Application Support/Johnny/Elixir Desktop/storage.json, shared by every instance). The plugin puts its
// contents in ELIXIR_NATIVE.store when it serves the page; each change goes back to it as an 'elixir' event.
(() => {
  const N = typeof ELIXIR_NATIVE === 'object' ? ELIXIR_NATIVE : { store: {} };
  const data = new Map(Object.entries(N.store || {}));
  const queue = [];
  const emit = (m) => {
    const b = window.__JUCE__ && window.__JUCE__.backend;
    if (!b) { queue.push(m); return; }
    while (queue.length) b.emitEvent('elixir', queue.shift());
    b.emitEvent('elixir', m);
  };
  window.elixirEmit = emit;
  const store = {
    getItem: (k) => (data.has(String(k)) ? data.get(String(k)) : null),
    setItem: (k, v) => { k = String(k); v = String(v); if (data.get(k) === v) return; data.set(k, v); emit({ t: 'store', k, v }); },
    removeItem: (k) => { k = String(k); if (!data.has(k)) return; data.delete(k); emit({ t: 'store', k, v: null }); },
    clear: () => { for (const k of [...data.keys()]) store.removeItem(k); },
    key: (i) => [...data.keys()][i] ?? null,
    get length() { return data.size; },
  };
  // errors go to the plugin's log (~/Library/Logs/Johnny/Elixir Desktop.log)
  window.addEventListener('error', (e) => emit({ t: 'log', s: `${e.message} at ${e.lineno}:${e.colno}` }));
  window.addEventListener('unhandledrejection', (e) => emit({ t: 'log', s: `unhandled: ${e.reason}` }));
  const cerr = console.error.bind(console);
  console.error = (...a) => { cerr(...a); emit({ t: 'log', s: a.map((x) => (x && x.stack) || String(x)).join(' ') }); };
  try { Object.defineProperty(window, 'localStorage', { value: store, configurable: true }); } catch (e) { /* the browser's own */ }
})();
