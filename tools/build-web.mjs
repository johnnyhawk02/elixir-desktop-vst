// Builds web/index.html, the plugin's UI: Elixir Desktop's own page (built by its build.mjs from ~/Developer/elixir-desktop,
// read only; build.mjs --out writes nowhere else) with two additions:
//   - at the top of <head>, a placeholder the plugin fills in when it serves the page (web/boot.js: the stored
//     settings and the sound to restore), so the page's storage reads see them from its first line
//   - after the page's code, web/shim.js, which sends the player messages to the plugin instead of an AudioWorklet
//   node tools/build-web.mjs [path to elixir-desktop]
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { join } from 'node:path';

const app = process.argv[2] || join(homedir(), 'Developer/elixir-desktop');
const web = new URL('../web/', import.meta.url).pathname;
const tmp = mkdtempSync(join(tmpdir(), 'elixir-web-'));
const out = join(tmp, 'index.html');
execFileSync('node', ['build.mjs', '--out', out], { cwd: app, stdio: 'inherit' });
let page = readFileSync(out, 'utf8');
rmSync(tmp, { recursive: true });

const boot = readFileSync(join(web, 'boot.js'), 'utf8');
const shim = readFileSync(join(web, 'shim.js'), 'utf8');
const css = readFileSync(join(web, 'plugin.css'), 'utf8');
if (!page.startsWith('<meta charset="utf-8">')) throw new Error('index.html: unexpected start');
// the plugin replaces /*@NATIVE@*/ with `const ELIXIR_NATIVE = {...};` when it serves the page
page = page.replace('<meta charset="utf-8">', () => `<meta charset="utf-8">\n<script>/*@NATIVE@*/\n${boot}</script>`);
// the web fonts load from Google when online; offline the page falls back to the system's
page = page.replace('</style>', () => `${css}</style>`);
const last = page.lastIndexOf('</script>');
page = page.slice(0, last + 9) + `\n<script>\n${shim}</script>` + page.slice(last + 9);
writeFileSync(join(web, 'index.html'), page);
console.log(`web/index.html: ${(page.length / 1024).toFixed(0)} KB`);
