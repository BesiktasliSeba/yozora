// the reference engine of tests/test_wasm.py: the same protocol as tools/yozora_wasm_run.c, answered by V8
const fs = require('fs');
const bytes = fs.readFileSync(process.argv[2]);
const view = new DataView(new ArrayBuffer(8));
const hex = (n) => '0x' + n.toString(16);
const typeOf = (kind) => kind;
function fromBits(type, text) {
    const v = BigInt(text);
    if (type === 'i32') return Number(BigInt.asIntN(32, v));
    if (type === 'i64') return BigInt.asIntN(64, v);
    if (type === 'f32') { view.setUint32(0, Number(BigInt.asUintN(32, v))); return view.getFloat32(0); }
    view.setBigUint64(0, BigInt.asUintN(64, v)); return view.getFloat64(0);
}
function toText(type, value) {
    if (type === 'i32') return 'i32:' + hex(value >>> 0);
    if (type === 'i64') return 'i64:' + hex(BigInt.asUintN(64, value));
    if (type === 'f32') { view.setFloat32(0, value); return 'f32:' + hex(view.getUint32(0)); }
    view.setFloat64(0, value); return 'f64:' + hex(view.getBigUint64(0));
}
let compiled;
try { compiled = new WebAssembly.Module(bytes); } catch (e) { console.log('invalid: ' + e.message); process.exit(1); }
const imports = {};
for (const imp of WebAssembly.Module.imports(compiled)) {
    imports[imp.module] = imports[imp.module] || {};
    if (imp.kind === 'function') imports[imp.module][imp.name] = (...a) => 0;
}
// the signature of an imported function is not visible from JavaScript, so a module of the tests that imports one uses the (i32) -> i32 shape
for (const imp of WebAssembly.Module.imports(compiled)) {
    if (imp.kind === 'function') imports[imp.module][imp.name] = (x) => (x * 2 + 1) | 0;
    if (imp.kind === 'memory') imports[imp.module][imp.name] = new WebAssembly.Memory({ initial: 1 });
}
const signatures = JSON.parse(process.env.WASM_SIGNATURES || '{}');
let instance;
try { instance = new WebAssembly.Instance(compiled, imports); } catch (e) { console.log((e instanceof WebAssembly.RuntimeError ? 'trap' : 'link') + ': ' + e.message); process.exit(1); }
const input = fs.readFileSync(0, 'utf8').split('\n');
for (const line of input) {
    const parts = line.trim().split(/\s+/).filter(Boolean);
    if (!parts.length) continue;
    const name = parts[0];
    const f = instance.exports[name];
    if (typeof f !== 'function') { console.log('error: no such export'); continue; }
    const sig = signatures[name];
    const args = parts.slice(1).map((p) => { const [t, v] = p.split(':'); return fromBits(t, v); });
    try {
        const r = f(...args);
        if (sig.results.length === 0) console.log('');
        else if (sig.results.length === 1) console.log(toText(sig.results[0], r));
        else console.log(r.map((x, i) => toText(sig.results[i], x)).join(' '));
    } catch (e) {
        console.log('trap ' + e.message);
    }
}
