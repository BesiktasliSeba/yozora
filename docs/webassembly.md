# webassembly

## why an own engine

javascriptcore of this revision has no webassembly on armv7: `ENABLE_WEBASSEMBLY` is forced off for `CPU(ARM)`. the slices built without it carry an engine of their own, and an arm64 slice built with `YOZORA_WASM=ON` keeps the one of javascriptcore and compiles none of this (`YOZORA_WASM_ENGINE` is off).

## the engine

- `core/wasm`: a decoder and validator that compile each function to a compact code array, and an interpreter over it. portable c99 with no dependency
- `core/core/yozora_wasm_api.js`: the javascript api, over natives in `core/core/YozoraWasm.cpp`
- the object is a lazy accessor on the global object: the api is built the first time a script reads it, a page that never asks pays nothing
- workers do not get `WebAssembly`, only the page's own world does

## supported

the core of the specification 2.0 without vectors:

- i32, i64, f32, f64, sign extension, saturating truncation, multi-value
- bulk memory and table instructions, reference types, several tables, mutable globals
- `WebAssembly.Module`, `Instance`, `Memory`, `Table`, `Global`, `validate`, `compile`, `instantiate`, `compileStreaming`, `instantiateStreaming`, `CompileError`, `LinkError`, `RuntimeError`
- i64 crosses as `BigInt`

## refused

with a `CompileError`: simd, threads and atomics (so no shared memory), exceptions, garbage collection types, tail calls, memory64, several memories, extended constant expressions.

- a memory has at most the 65536 pages (4 GiB) of the specification, a growth the system has no room for returns -1 as the specification says. there is no cap of the engine below that
- `Memory.buffer` cannot be transferred and goes empty when the memory grows

## limits

- an interpreter is far slower than the jit of other engines, a module with heavy loops such as a codec or a game will be slow on a phone
- an external reference is kept for the life of the thread because the engine counts none, objects once each, at most one million. a page that passes endless fresh values through `externref` ends with a `RangeError`
- a script stopped by the core (`yozora_stop_scripts`) stops a loop inside webassembly too: the interpreter asks the vm every 65536 branches and calls

## tests

- `meow test` runs `tests/test_wasm_engine.c`: the interrupt hook, the memory observer and its destroy
- `meow test wasm` runs `tests/test_wasm.py`: 136 numeric instructions called with edge values (about 41000 calls), the bits must be the ones v8 gives, needs node
- `tests/test_core_wasm.py` runs one script of about 130 checks under v8 and in a page of `yozora-render`, every printed line must be the same
- `tests/test_core_wasm_stop.py` stops a loop that never returns
- modules are assembled by `tests/wasm_builder.py`
