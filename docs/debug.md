# debug

## in the app

developer settings (a section with its own glyphs):

- debug: a card in the corner of the page with the frames of the interface and the scenes put on screen each second, the processor load of the process, the share of time the core spends in layout and painting, resident memory against the memory of the phone, the bytes received and sent by the page loader and downloads (the media proxy is not counted), the phone by name (`core/yozora_debug.c` maps the system identifier) and the host of the page. there is no gpu line, see [graphics](graphics.md)
- logging: on by default, switched off by the person only. keeps the stderr of the app (`NSLog`, the core, console errors of pages) in a buffer of 256 KB and passes it on. the capture starts in `main`, right after the crash recorder, so the splash and the engine start are in the file of the run
- logs: opens it as a terminal card (`YZLogController`): this run, the run before, crashes, a filter by level (all, info, important, errors), copy and clear. the file of a run is kept up to 1 MB and the run before is kept as `yozora-previous.log`
- jit: a switch and a line with what javascriptcore reports, read once at start. on by default on armv7, off on arm64 until it has run on a device

the core writes lines as `HH:MM:SS.mmm L category: message`, `core/yozora_log.c`. a line has a level (D, I, W, E), a short category and no line break.

## crashes

the app keeps its own record of the last run (`YZCrash`, `Library/Caches/yozora-crash.log`): the screen it was on, notes at the steps of a launch, memory warnings, faults. the log screen shows it as crashes. the recorder is installed first in `main`, before javascriptcore starts, and the notes `launch: javascriptcore start` and `launch: javascriptcore ready` bracket that step: a record with the first and not the second means the launch died while the engine started (the jit maps its executable region there). a crash before `main` (dyld, a static initializer) leaves no record, only the system crash report. a handler also echoes the whole record to stderr, so it lands in `yozora.log` and, after the restart, in `yozora-previous.log`. a fault whose pc is in no image (generated code, null included) is written to the record as `fault in generated code, pc …`. the link writes `build/<slice>/Yozora.map`, which names every function including the local ones that `strip -x` removes from the binary, so an address of a report can be read against the map of the build it came from (keep it with the ipa). a signal handler writes the addresses of the stack and `tools/yozora_crash.py LOG UNSTRIPPED_BINARY` symbolizes them. after the engine starts the record gets a line `jit: …` with the tiers that are on, whether the dfg compiles concurrently and the size of the code pool, so a fault in generated code says which tier could have made it.

the system crash report of a build is read with `tools/yozora_ips.py REPORT.ips [build/symbols]`. every link of a slice keeps its map under the uuid of the binary (`scripts/archive_symbols.py`, the `symbols-armv7` and `symbols-arm64` steps, `build/symbols/UUID.map`), the report names its build by `slice_uuid`, and the tool prints the functions of the crashed thread, the local ones included. a report of a build whose map was not kept says so and lists the maps that exist: the 1.3.0 reports of 2026-11-22 could not be read for that reason, the binary of that build has no local symbols.

## environment

- `YOZORA_LOG_REQUESTS=1`: every request and failure, blocked requests as `net: blocked HOST`
- `YOZORA_GL_TRACE=1`: forwarded gl calls that left an error
- `YOZORA_NO_JIT=1`: turns the jit off at run time
- `YOZORA_RASTER_THREADS=1..4`: raster threads
- `YOZORA_DEVICE_MEMORY_MB`: the memory the core acts on
- `YOZORA_RENDER_BUSY=1`: the share of each second the core thread worked
- `YOZORA_RENDER_BYTECODE=DIR`: the bytecode cache of a run and what it did, mode 0700
- `YOZORA_RENDER_PROFILE=FILE` (`_US` for the interval, `_DEPTH` for the frames): samples the running threads

## tools

- `yozora-render`: the headless page runner. it prints the page console, and on a fatal signal the addresses of the stack
- `tools/yozora_profile.py FILE UNSTRIPPED_BINARY`: self time, inclusive time and phases (`--core`, `--callers NAME`)
- `tools/yozora_crash.py`: the symbolizer
- `tools/yzdev.m`: screenshot, launch by bundle id and synthesized touches for driving the app over a shell on a jailbroken phone, needs `tools/yzdev.entitlements`
- `yozora-js`: a javascript shell of the same engine, `tests/js/features.js` is its feature script
- `tools/yozora_proxy_cli.c`, `tools/yozora_store_cli.c`, `tools/yozora_wasm_run.c`: the proxy, the store and the webassembly engine from a shell
