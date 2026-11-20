"""the WebAssembly of the page on a slice without the one of JavaScriptCore: the same script runs under V8 (node) and under the engine of core/wasm behind the API of
core/core/yozora_wasm_api.js, and every line it prints must be the same. the modules are assembled in tests/wasm_builder.py

run: python3 -B tests/test_core_wasm.py build/core/webcore-host-gl/bin/yozora-render
"""
import base64
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wasm_builder import *  # noqa: F401,F403

ROOT = Path(__file__).resolve().parent.parent


def modules():
    out = {}

    m = Module()
    m.func([I32, I32], [I32], LOCAL_GET(0) + LOCAL_GET(1) + I32_ADD, export="add")
    m.func([I64], [I64], LOCAL_GET(0) + I64_EQZ + IF(I64) + I64_CONST(1) + ELSE + LOCAL_GET(0) + LOCAL_GET(0) + I64_CONST(1) + I64_SUB + CALL(1) + I64_MUL + END, export="fac")
    m.func([F32, F32], [F32], LOCAL_GET(0) + LOCAL_GET(1) + F32_ADD, export="fadd32")
    m.func([F64, F64], [F64], LOCAL_GET(0) + LOCAL_GET(1) + F64_MUL, export="fmul64")
    m.func([I32, I32], [I32, I32], LOCAL_GET(1) + LOCAL_GET(0), export="swap")
    m.func([I32, I32], [I32], LOCAL_GET(0) + LOCAL_GET(1) + I32_DIV_S, export="divs")
    m.func([], [], UNREACHABLE, export="unreach")
    m.func([I32], [I32], LOCAL_GET(0) + CALL(7) , export="rec")
    m.func([I64], [I64], LOCAL_GET(0), export="id64")
    m.func([F32], [F32], LOCAL_GET(0), export="id32")
    m.func([F64], [F64], LOCAL_GET(0), export="id64f")
    m.func([EXTERNREF], [EXTERNREF], LOCAL_GET(0), export="idext")
    m.func([FUNCREF], [FUNCREF], LOCAL_GET(0), export="idfn")
    m.func([], [], b"", export="nothing")
    m.func([I32, I64, F32, F64], [F64], LOCAL_GET(3), export="mixed")
    m.custom("note", b"hello")
    m.custom("note", b"again")
    out["math"] = m.build()

    m = Module()
    m.memory(1, 4, export="mem")
    m.global_(I32, True, I32_CONST(5), export="counter")
    m.global_(I64, False, I64_CONST(-2), export="big")
    m.global_(F64, False, b"\x44" + struct.pack("<d", 1.5), export="half")
    m.table(FUNCREF, 3, 10, export="tab")
    m.func([I32, I32], [I32], LOCAL_GET(0) + LOCAL_GET(1) + I32_ADD, export="add")
    m.func([I32, I32], [I32], LOCAL_GET(0) + LOCAL_GET(1) + I32_SUB, export="sub")
    m.func([I32, I32], [], LOCAL_GET(0) + LOCAL_GET(1) + I32_STORE(), export="store")
    m.func([I32], [I32], LOCAL_GET(0) + I32_LOAD(), export="load")
    m.func([I32], [I32], LOCAL_GET(0) + MEMORY_GROW, export="grow")
    m.func([], [I32], MEMORY_SIZE, export="size")
    m.func([I32, I32, I32], [I32], LOCAL_GET(1) + LOCAL_GET(2) + LOCAL_GET(0) + CALL_INDIRECT(m.type([I32, I32], [I32])), export="callt")
    m.func([], [I32], GLOBAL_GET(0) + I32_CONST(1) + I32_ADD + GLOBAL_SET(0) + GLOBAL_GET(0), export="bump")
    m.func([I32], [I32], LOCAL_GET(0) + I32_LOAD8_U(), export="load8")
    m.element(0, [0, 1])
    out["state"] = m.build()

    m = Module()
    m.import_func("env", "f", [I32], [I32])
    m.import_memory("env", "mem", 1)
    m.import_table("env", "tab", FUNCREF, 1)
    m.import_global("env", "g", I32)
    m.import_func("env", "wide", [I64, F32, F64], [I64])
    m.import_func("env", "pair", [], [I32, I32])
    m.import_func("env", "ext", [EXTERNREF], [EXTERNREF])
    m.import_func("env", "boom", [], [])
    m.func([I32], [I32], LOCAL_GET(0) + CALL(0) + GLOBAL_GET(0) + I32_ADD, export="run")
    m.func([I64], [I64], LOCAL_GET(0) + b"\x43\x00\x00\xc0\x3f" + b"\x44" + bytes.fromhex("000000000000f83f") + CALL(1), export="callwide")
    m.func([], [I32], CALL(2) + I32_ADD, export="callpair")
    m.func([EXTERNREF], [EXTERNREF], LOCAL_GET(0) + CALL(3), export="callext")
    m.func([], [], CALL(4), export="callboom")
    m.func([], [], I32_CONST(0) + I32_CONST(7) + I32_STORE(), export="start_body")
    m.start = m.imported_funcs() + 5
    out["imports"] = m.build()

    m = Module()
    m.import_global("env", "mg", I32, True)
    m.func([], [I32], GLOBAL_GET(0), export="read")
    out["mutimport"] = m.build()
    return out


SCRIPT = r"""
var out = [];
function emit(name, fn) {
    var text;
    try { text = String(fn()); } catch (e) { text = 'threw ' + (e && e.constructor === WebAssembly.RuntimeError ? 'RuntimeError' : e && e.constructor === WebAssembly.CompileError ? 'CompileError' : e && e.constructor === WebAssembly.LinkError ? 'LinkError' : e && e.constructor === RangeError ? 'RangeError' : e && e.constructor === TypeError ? 'TypeError' : 'other:' + (e && e.message)); }
    log(name + ' = ' + text);
}
function bytes(name) {
    var raw = atob(MODULES[name]), out = new Uint8Array(raw.length);
    for (var i = 0; i < raw.length; ++i) out[i] = raw.charCodeAt(i);
    return out;
}
var W = WebAssembly;
var math = new W.Instance(new W.Module(bytes('math'))).exports;
var state = new W.Instance(new W.Module(bytes('state'))).exports;

emit('typeof', function () { return typeof W + ' ' + Object.prototype.toString.call(W); });
emit('names', function () { return ['CompileError', 'Global', 'Instance', 'LinkError', 'Memory', 'Module', 'RuntimeError', 'Table', 'compile', 'compileStreaming', 'instantiate', 'instantiateStreaming', 'validate'].map(function (k) { return typeof W[k]; }).join(','); });
emit('enumerable', function () { return ['Module', 'validate', 'compile'].map(function (k) { return Object.getOwnPropertyDescriptor(W, k).enumerable; }).join(','); });
emit('validate good', function () { return W.validate(bytes('math')); });
emit('validate bad', function () { return W.validate(new Uint8Array([0, 1, 2, 3])); });
emit('validate type', function () { return W.validate('x'); });
emit('module bad', function () { return new W.Module(new Uint8Array([0, 97, 115, 109, 2, 0, 0, 0])); });
emit('module empty', function () { return new W.Module(new Uint8Array(0)); });
emit('module truncated', function () { var b = bytes('math'); return new W.Module(b.subarray(0, b.length - 3)); });
emit('module call', function () { return W.Module(bytes('math')); });
emit('module arg', function () { return new W.Module(5); });
emit('module noarg', function () { return new W.Module(); });
emit('module tag', function () { return Object.prototype.toString.call(new W.Module(bytes('math'))); });
emit('imports', function () { return W.Module.imports(new W.Module(bytes('imports'))).map(function (i) { return i.module + '.' + i.name + ':' + i.kind; }).join(' '); });
emit('exports', function () { return W.Module.exports(new W.Module(bytes('state'))).map(function (i) { return i.name + ':' + i.kind; }).join(' '); });
emit('customSections', function () { var m = new W.Module(bytes('math')); var a = W.Module.customSections(m, 'note'); return a.length + ' ' + a.map(function (b) { return b.byteLength + ':' + new Uint8Array(b)[0]; }).join(',') + ' ' + W.Module.customSections(m, 'none').length; });
emit('instance noimports', function () { return new W.Instance(new W.Module(bytes('math'))) instanceof W.Instance; });
emit('instance needs imports', function () { return new W.Instance(new W.Module(bytes('imports'))); });
emit('instance bad module', function () { return new W.Instance({}); });
emit('instance call', function () { return W.Instance(new W.Module(bytes('math'))); });

emit('add', function () { return math.add(2, 40) + ' ' + math.add(2147483647, 1) + ' ' + math.add(-1, -1) + ' ' + math.add('7', 3.9) + ' ' + math.add(undefined, 1) + ' ' + math.add(4294967297, 0); });
emit('add bigint', function () { return math.add(1n, 1); });
emit('add missing', function () { return math.add(); });
emit('fac', function () { return math.fac(20n) + ' ' + math.fac(21n) + ' ' + math.fac(25n); });
emit('fac number', function () { return math.fac(5); });
emit('id64', function () { return [0n, -1n, 2n ** 63n, 2n ** 64n + 5n, -(2n ** 63n), '12'].map(function (v) { return math.id64(v); }).join(' '); });
emit('id32', function () { return [0.1, 1e40, -0, NaN, Infinity, 16777217].map(function (v) { var r = math.id32(v); return Object.is(r, -0) ? '-0' : r; }).join(' '); });
emit('id64f', function () { return [0.1, 1e300, -0, NaN, -Infinity, 5e-324].map(function (v) { var r = math.id64f(v); return Object.is(r, -0) ? '-0' : r; }).join(' '); });
emit('fadd32', function () { return math.fadd32(0.1, 0.2); });
emit('fmul64', function () { return math.fmul64(0.1, 3); });
emit('mixed', function () { return math.mixed(1, 2n, 3, 4.5); });
emit('swap', function () { return JSON.stringify(math.swap(1, 2)) + ' ' + Array.isArray(math.swap(1, 2)); });
emit('nothing', function () { return math.nothing() === undefined; });
emit('divs zero', function () { return math.divs(1, 0); });
emit('divs overflow', function () { return math.divs(-2147483648, -1); });
emit('divs ok', function () { return math.divs(-7, 2); });
emit('unreachable', function () { return math.unreach(); });
emit('recursion', function () { return math.rec(1); });
emit('recover after trap', function () { try { math.unreach(); } catch (e) {} return math.add(1, 2); });
emit('fn name', function () { return math.add.name + ' ' + math.fac.name + ' ' + math.mixed.length + ' ' + math.add.length; });
emit('fn typeof', function () { return typeof math.add + ' ' + (math.add instanceof Function); });
emit('fn new', function () { return new math.add(1, 2); });
emit('exports frozen', function () { return Object.isFrozen(math) + ' ' + (Object.getPrototypeOf(math) === null) + ' ' + Object.keys(math).length; });
emit('exports write', function () { 'use strict'; math.add = 1; });
emit('idext', function () { var o = {}; return (math.idext(o) === o) + ' ' + math.idext(undefined) + ' ' + math.idext(null) + ' ' + math.idext(5) + ' ' + math.idext('s'); });
emit('idfn', function () { return (math.idfn(math.add) === math.add) + ' ' + math.idfn(null); });
emit('idfn bad', function () { return math.idfn(function () {}); });
emit('idfn nonfn', function () { return math.idfn(1); });

emit('memory buffer', function () { return state.mem.buffer.byteLength + ' ' + (state.mem.buffer === state.mem.buffer) + ' ' + (state.mem instanceof W.Memory); });
emit('memory store/load', function () { state.store(8, 0x01020304); var v = new Uint8Array(state.mem.buffer); return v[8] + ' ' + v[9] + ' ' + v[11] + ' ' + state.load(8) + ' ' + state.load8(9); });
emit('memory js write', function () { new Uint8Array(state.mem.buffer)[100] = 77; return state.load8(100); });
emit('memory oob', function () { return state.load(65533); });
emit('memory oob store', function () { state.store(65536, 1); });
emit('memory size', function () { return state.size(); });
emit('memory grow wasm', function () { var before = state.mem.buffer; var r = state.grow(1); var after = state.mem.buffer; return r + ' ' + before.byteLength + ' ' + after.byteLength + ' ' + state.size() + ' ' + (before === after); });
emit('memory grow data kept', function () { return state.load(8) + ' ' + new Uint8Array(state.mem.buffer)[100]; });
emit('memory grow js', function () { var b = state.mem.buffer; var r = state.mem.grow(1); return r + ' ' + b.byteLength + ' ' + state.mem.buffer.byteLength; });
emit('memory grow fail wasm', function () { return state.grow(5); });
emit('memory grow fail js', function () { return state.mem.grow(5); });
emit('memory view after grow', function () { var v = new Uint8Array(state.mem.buffer); state.mem.grow(0); return v.length + ' ' + state.mem.buffer.byteLength; });
emit('memory new', function () { var m = new W.Memory({ initial: 2, maximum: 3 }); var b = m.buffer; return b.byteLength + ' ' + m.grow(1) + ' ' + b.byteLength + ' ' + m.buffer.byteLength; });
emit('memory new noinitial', function () { return new W.Memory({}); });
emit('memory new nodesc', function () { return new W.Memory(); });
emit('memory new max<initial', function () { return new W.Memory({ initial: 3, maximum: 1 }); });
emit('memory new call', function () { return W.Memory({ initial: 1 }); });
emit('memory transfer', function () { return new W.Memory({ initial: 1 }).buffer.transfer ? 'has' : 'none'; });
emit('memory transfer call', function () { return new W.Memory({ initial: 1 }).buffer.transfer(); });
emit('memory transfer kept', function () { var m = new W.Memory({ initial: 1 }); try { m.buffer.transfer(); } catch (e) {} return m.buffer.byteLength + ' ' + m.grow(1) + ' ' + m.buffer.byteLength; });
emit('memory slice', function () { var m = new W.Memory({ initial: 1 }); return m.buffer.slice(0, 8).byteLength; });
emit('memory tag', function () { return Object.prototype.toString.call(new W.Memory({ initial: 0 })); });
emit('memory zero', function () { return new W.Memory({ initial: 0 }).buffer.byteLength; });
emit('global mutable', function () { return state.counter.value + ' ' + state.bump() + ' ' + state.counter.value + ' ' + (state.counter.value = 40) + ' ' + state.bump(); });
emit('global immutable i64', function () { return state.big.value + ' ' + typeof state.big.value; });
emit('global immutable set', function () { state.big.value = 1n; });
emit('global f64', function () { return state.half.value + ' ' + state.half.valueOf(); });
emit('global new', function () { var g = new W.Global({ value: 'i32', mutable: true }, 9); g.value = 11; return g.value + ' ' + new W.Global({ value: 'i64' }).value + ' ' + new W.Global({ value: 'f32' }, 0.1).value + ' ' + new W.Global({ value: 'externref' }).value + ' ' + new W.Global({ value: 'anyfunc' }).value; });
emit('global new bad', function () { return new W.Global({ value: 'i16' }); });
emit('global new i64 number', function () { return new W.Global({ value: 'i64' }, 5); });
emit('global tag', function () { return Object.prototype.toString.call(state.counter); });
emit('table length', function () { return state.tab.length + ' ' + (state.tab instanceof W.Table); });
emit('table get', function () { return (state.tab.get(0) === state.add) + ' ' + (state.tab.get(0) === state.tab.get(0)) + ' ' + state.tab.get(1).name + ' ' + state.tab.get(2); });
emit('table get null', function () { var t = new W.Table({ element: 'anyfunc', initial: 2 }); return t.get(0) + ' ' + t.length; });
emit('table oob', function () { return state.tab.get(3); });
emit('table call_indirect', function () { return state.callt(0, 3, 4) + ' ' + state.callt(1, 1, 2); });
emit('table call_indirect null', function () { return state.callt(2, 1, 2); });
emit('table set', function () { state.tab.set(2, state.tab.get(0)); return state.callt(2, 5, 6); });
emit('table set null', function () { state.tab.set(2, null); return state.tab.get(2); });
emit('table set bad', function () { state.tab.set(2, function () {}); });
emit('table set oob', function () { state.tab.set(10, null); });
emit('table grow', function () { return state.tab.grow(2) + ' ' + state.tab.length; });
emit('table grow fail', function () { return state.tab.grow(100); });
emit('table new', function () { var t = new W.Table({ element: 'externref', initial: 2, maximum: 4 }, 'x'); var o = {}; t.set(1, o); return t.get(0) + ' ' + (t.get(1) === o) + ' ' + t.grow(1, 7) + ' ' + t.get(2); });
emit('table new bad', function () { return new W.Table({ element: 'i32', initial: 1 }); });
emit('table new anyfunc init', function () { return new W.Table({ element: 'anyfunc', initial: 1 }, math.add).get(0) === math.add; });
emit('table tag', function () { return Object.prototype.toString.call(state.tab); });

function host() {
    var calls = [];
    var memory = new W.Memory({ initial: 1 });
    var table = new W.Table({ element: 'anyfunc', initial: 2 });
    var env = {
        f: function (x) { calls.push('f' + x); return x * 2; },
        mem: memory,
        tab: table,
        g: 100,
        wide: function (a, b, c) { calls.push('wide ' + typeof a + ' ' + a + ' ' + b + ' ' + c); return a + 1n; },
        pair: function () { return [3, 4]; },
        ext: function (x) { calls.push('ext ' + typeof x); return x; },
        boom: function () { throw env.error; },
        error: new Error('boom')
    };
    return { env: env, calls: calls, memory: memory, table: table };
}
emit('imports instantiate', function () { var h = host(); var i = new W.Instance(new W.Module(bytes('imports')), { env: h.env }); return i.exports.run(21) + ' ' + h.calls.join('|'); });
emit('imports start', function () { var h = host(); new W.Instance(new W.Module(bytes('imports')), { env: h.env }); return new Uint8Array(h.memory.buffer)[0]; });
emit('imports wide', function () { var h = host(); var i = new W.Instance(new W.Module(bytes('imports')), { env: h.env }); return i.exports.callwide(41n) + ' ' + h.calls.slice(-1); });
emit('imports multi', function () { var h = host(); var i = new W.Instance(new W.Module(bytes('imports')), { env: h.env }); return i.exports.callpair(); });
emit('imports extref', function () { var h = host(); var i = new W.Instance(new W.Module(bytes('imports')), { env: h.env }); var o = {}; return (i.exports.callext(o) === o) + ' ' + i.exports.callext('s') + ' ' + i.exports.callext(undefined) + ' ' + i.exports.callext(null) + ' ' + h.calls.slice(-1); });
emit('imports throw', function () { var h = host(); var i = new W.Instance(new W.Module(bytes('imports')), { env: h.env }); try { i.exports.callboom(); } catch (e) { return e === h.env.error; } });
emit('imports throw then run', function () { var h = host(); var i = new W.Instance(new W.Module(bytes('imports')), { env: h.env }); try { i.exports.callboom(); } catch (e) {} return i.exports.run(1); });
emit('imports result coercion', function () { var h = host(); h.env.f = function () { return '12.9'; }; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }).exports.run(0); });
emit('imports result undefined', function () { var h = host(); h.env.f = function () {}; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }).exports.run(0); });
emit('imports pair bad', function () { var h = host(); h.env.pair = function () { return [1]; }; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }).exports.callpair(); });
emit('imports pair notiterable', function () { var h = host(); h.env.pair = function () { return 5; }; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }).exports.callpair(); });
emit('imports wide number result', function () { var h = host(); h.env.wide = function () { return 1; }; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }).exports.callwide(1n); });
emit('imports no object', function () { return new W.Instance(new W.Module(bytes('imports')), 5); });
emit('imports no module', function () { return new W.Instance(new W.Module(bytes('imports')), {}); });
emit('imports not function', function () { var h = host(); h.env.f = 3; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }); });
emit('imports memory wrong', function () { var h = host(); h.env.mem = {}; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }); });
emit('imports memory small', function () { var h = host(); h.env.mem = new W.Memory({ initial: 0 }); return new W.Instance(new W.Module(bytes('imports')), { env: h.env }); });
emit('imports table wrong', function () { var h = host(); h.env.tab = []; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }); });
emit('imports global wrong', function () { var h = host(); h.env.g = 'x'; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }); });
emit('imports global object', function () { var h = host(); h.env.g = new W.Global({ value: 'i32' }, 5); return new W.Instance(new W.Module(bytes('imports')), { env: h.env }).exports.run(0); });
emit('imports global bigint for i32', function () { var h = host(); h.env.g = 5n; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }); });
emit('imports exported fn', function () { var h = host(); h.env.f = math.add; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }); });
emit('imports exported fn right type', function () { var h = host(); h.env.f = state.load; return new W.Instance(new W.Module(bytes('imports')), { env: h.env }).exports.run(8); });
emit('imports mutable global plain', function () { return new W.Instance(new W.Module(bytes('mutimport')), { env: { mg: 5 } }); });
emit('imports mutable global', function () { var g = new W.Global({ value: 'i32', mutable: true }, 3); var i = new W.Instance(new W.Module(bytes('mutimport')), { env: { mg: g } }); var a = i.exports.read(); g.value = 8; return a + ' ' + i.exports.read(); });
emit('imports immutable for mutable', function () { var g = new W.Global({ value: 'i32' }, 3); return new W.Instance(new W.Module(bytes('mutimport')), { env: { mg: g } }); });
emit('shared memory', function () { var h = host(); var i = new W.Instance(new W.Module(bytes('imports')), { env: h.env }); new Uint8Array(h.memory.buffer)[0] = 9; var j = new W.Instance(new W.Module(bytes('state')), {}); return new Uint8Array(h.memory.buffer)[0] + ' ' + (i instanceof W.Instance) + ' ' + (j.exports.mem !== h.memory); });
emit('memory identity export', function () { var h = host(); var i = new W.Instance(new W.Module(bytes('state')), {}); return i.exports.mem === i.exports.mem; });

emit('errors', function () { return [W.CompileError, W.LinkError, W.RuntimeError].map(function (c) { var e = new c('m'); return (e instanceof Error) + ' ' + e.name + ' ' + e.message + ' ' + Object.prototype.toString.call(e); }).join(' | '); });
emit('error ctor', function () { return W.RuntimeError.name + ' ' + W.RuntimeError.length; });

var chain = Promise.resolve();
function later(name, fn) { chain = chain.then(function () { return fn(); }).then(function (v) { log(name + ' = ' + v); }, function (e) { log(name + ' = rejected ' + (e && e.constructor === W.CompileError ? 'CompileError' : e && e.constructor === W.LinkError ? 'LinkError' : e && e.constructor === TypeError ? 'TypeError' : e && e.constructor === W.RuntimeError ? 'RuntimeError' : 'other')); }); }
later('compile', function () { return W.compile(bytes('math')).then(function (m) { return m instanceof W.Module; }); });
later('compile bad', function () { return W.compile(new Uint8Array(4)); });
later('compile type', function () { return W.compile('x'); });
later('instantiate bytes', function () { return W.instantiate(bytes('math')).then(function (r) { return Object.keys(r).join(',') + ' ' + r.instance.exports.add(2, 3); }); });
later('instantiate module', function () { return W.instantiate(new W.Module(bytes('math'))).then(function (r) { return (r instanceof W.Instance) + ' ' + r.exports.add(2, 3); }); });
later('instantiate imports', function () { var h = host(); return W.instantiate(bytes('imports'), { env: h.env }).then(function (r) { return r.instance.exports.run(4); }); });
later('instantiate link error', function () { return W.instantiate(bytes('imports'), { env: {} }); });
later('instantiate trap start', function () { var h = host(); h.env.f = function () { throw new Error('x'); }; return W.instantiate(bytes('imports'), { env: h.env }).then(function () { return 'ok'; }); });
later('streaming present', function () { return typeof W.compileStreaming + ' ' + typeof W.instantiateStreaming; });
chain.then(function () { log('done'); });
"""


def script_with_modules():
    mods = {name: base64.b64encode(data).decode() for name, data in modules().items()}
    return "var MODULES = " + json.dumps(mods) + ";\n" + SCRIPT


def run_node():
    node = shutil.which("node")
    if not node:
        raise SystemExit("node is needed as the reference engine")
    program = "var log = function (s) { console.log(s); };\n" + script_with_modules()
    result = subprocess.run([node, "-e", program], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    return result.stdout.splitlines()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    page = b""

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(self.page)))
        self.end_headers()
        self.wfile.write(self.page)


def run_page(binary):
    page = ("<!doctype html><html><head><meta charset=utf-8><title>wasm</title></head><body><script>var log = function (s) { console.log(s); };\n"
            + script_with_modules() + "</script></body></html>").encode()
    Handler.page = page
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="yozora-wasm-") as directory:
        environment = {"YOZORA_RENDER_SETTLE_MS": "3000", "PATH": "/usr/bin:/bin"}
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                                 f"http://127.0.0.1:{server.server_address[1]}/", str(Path(directory) / "page.png")],
                                capture_output=True, text=True, timeout=180, env=environment)
    server.shutdown()
    assert result.returncode == 0, result
    lines = []
    for line in result.stderr.splitlines():
        match = re.match(r"console: log: (.*) \(http://127\.0\.0\.1:\d+/:\d+\)$", line)
        if match:
            lines.append(match.group(1))
        elif line.startswith("console: "):
            lines.append("UNEXPECTED " + line)
    return lines


def main():
    binary = str(Path(sys.argv[1]).resolve())
    expected = run_node()
    actual = run_page(binary)
    failures = 0
    for index in range(max(len(expected), len(actual))):
        want = expected[index] if index < len(expected) else "<missing>"
        got = actual[index] if index < len(actual) else "<missing>"
        if want != got:
            failures += 1
            print(f"DIFF line {index + 1}\n  v8:     {want}\n  yozora: {got}")
    assert expected and expected[-1] == "done", "the reference run did not finish"
    assert actual and actual[-1] == "done", "the page did not finish"
    if failures:
        raise SystemExit(f"{failures} of {len(expected)} lines differ from V8")
    print(f"core wasm: {len(expected)} lines of the page script are the same under V8 and the engine of core/wasm")


if __name__ == "__main__":
    main()
