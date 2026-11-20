# javascript

## engines

javascriptcore of the same revision as webcore, linked statically.

- armv7 and arm64: baseline jit, dfg jit and regexp jit
- when the system refuses executable memory the assembler interpreter of the same binary runs, there is no second build for it
- `YOZORA_ARMV7_JIT=0` at build time builds the c loop instead of the jit, `YOZORA_NO_JIT=1` turns the jit off at run time
- ftl is off, there is no llvm on the phone

the jit needs memory that is both writable and executable. the entitlement is signed in `tools/yozora-jit.entitlements`, and the core reserves the executable region early, on the core thread, before uikit starts.

## what was fixed in the port

the 32 bit arm jit of this revision did not run as shipped. `core/patches/webkit-yozora.patch` carries the changes: linked property caches, the darwin calling convention for doubles, concurrent compilation, map and set iterators under dfg. each has a regression in `tests/jit_device.sh` that requires optimized compilation to happen.

## settings

developer settings show a jit switch and a line with what javascriptcore reports after it started: baseline, dfg, regexp, or why it is off. the switch is read once in `main`, it counts at the next launch.

## bytecode cache

a script of 16 KB or more keeps its bytecode in `Library/Caches/YozoraBytecode`, 64 MB. a script that loads again skips the parser and the generator. the key is the text of the script and the host, not the address, so a script served from a changing url still hits.

- a separate writer thread owns the disk, the page never waits for it
- pending bytes are capped by the memory tier of the phone (6 MB at 512 MB and below)
- cleared with the history
- the private session reads and writes none of it

## scripts that do not return

see [core](core.md): a stuck script is offered to be stopped after 8 seconds, and every long loop checks for it, webassembly included.

## stack

pages that recurse until the stack is full were run with several stack limits, with and without the jit, by `tests/stack_device.py`. the result is a `RangeError`, not a crash.

## measurements

baseline measurements against the c loop were fib 1.5x, loops 1.7x and regexps 5x, property inline caches gave 2x to 4x in property loops. dfg has its own comparison in `tests/jit_device.sh`. real sites are loaded by `tests/site_stress_device.sh`.
