// Runs the original tgb420.js engine with Max stubs; prints note-ons as "ms pitch vel durMs"
const fs = require('fs'), vm = require('vm');
const src = fs.readFileSync(process.argv[2], 'utf8');
const cfg = fs.readFileSync(0, 'utf8').split('\n').filter(Boolean).map(l => l.trim().split(/\s+/));
const ctx = { post() {}, outlet() {}, Task: function () { this.schedule = () => {}; this.cancel = () => {}; },
              LiveAPI: function () {}, arrayfromargs: (m, a) => [m].concat(Array.from(a)), Math, JSON, String, Number, Array, Object, parseInt };
vm.createContext(ctx);
vm.runInContext(src, ctx);
let quarters = 16, held = [], seed = 1;
const run = (code) => vm.runInContext(code, ctx);
for (const [k, ...v] of cfg) {
  const n = v.map(Number);
  if (['pitch','oct','vel','gate','chance','aux','mode','div','len'].includes(k)) run(`S.${k} = ${JSON.stringify(n)}`);
  else if (k === 'llen') n.forEach((x, i) => run(`G.llen${i + 1} = ${x}`));
  else if (k === 'held') held = n;
  else if (k === 'arpseed') seed = n[0];
  else if (k === 'quarters') quarters = n[0];
  else if (k === 'scalemode') run(`G.scalemode = ${n[0] === 1 ? 2 : 0}`);   // device: 2 = Custom
  else run(`G.${k} = ${n[0]}`);
}
run(`G.tempo = 120; held = ${JSON.stringify(held)}; arpSeed = ${seed};`);
const pulseMs = run('pulseMs()');
const total = Math.round(quarters * 500 / pulseMs);
const out = [];
for (let k = 0; k < total; k++) {
  run(`cur = { k: ${k}, note: null, aux: null, ui: null }; simTick(${k});`);
  const note = run('cur.note');
  if (note) out.push(`${(k * pulseMs + note[3]).toFixed(1)} ${note[0]} ${note[1]} ${note[2].toFixed(1)}`);
}
console.log(out.join('\n'));
