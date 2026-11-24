# debug

## in the app

developer settings (a section with its own glyphs):

- debug: a card in the corner of the page with the frames of the interface and the scenes put on screen each second, the processor load of the process, the share of time the core spends in layout and painting, resident memory against the memory of the phone, the bytes received and sent by the page loader and downloads (the media proxy is not counted), the phone by name (`core/yozora_debug.c` maps the system identifier) and the host of the page. there is no gpu line, see [graphics](graphics.md)
- logging: on by default, switched off by the person only. keeps the stderr of the app (`NSLog`, the core, console errors of pages) in a buffer of 256 KB and passes it on. the capture starts in `main`, right after the crash recorder, so the splash and the engine start are in the file of the run
- logs: opens it as a terminal card (`YZLogController`): this run, the run before, crashes, a filter by level (all, info, important, errors), copy and clear. the file of a run is kept up to 1 MB and the run before is kept as `yozora-previous.log`
- all designs: unlocks the classic design on ios 8 and later (settings > for developers). turning it on asks first, on a card with the error sticker of the mascot, whether the person agrees to meet bugs and rough edges of a design made for other systems. turning it off puts a stored classic choice away again
- jit: a switch and a line with what javascriptcore reports, read once at start. on by default on armv7, off on arm64 until it has run on a device

the core writes lines as `HH:MM:SS.mmm L category: message`, `core/yozora_log.c`. a line has a level (D, I, W, E), a short category and no line break.

## crashes

the app keeps its own record, `Library/Caches/yozora-crash.log` (`YZCrash`, 256 KB, the older half is kept as `.1`), whatever the system does. the log screen shows it as crashes. every event is one block that starts with `==== KIND date ====` and reads as a table: what happened, a verdict in words, the state (foreground or background, seconds since the launch), app, binary (uuid and slide), system (model, ios, memory), the memory of the app and of the phone, the last screen, the last forty eight notes, and the stack of every thread with its name, its state (running or waiting) and its cpu share. the blocks:

- `CRASH`: a signal (segv, bus, ill, fpe, abrt, trap, sys) or an uncaught exception. for a signal the block has the registers, the instructions at pc and at lr (the instruction before a return address is the call, and the register it names is the one that held a null), the crashed thread first and the other threads as they were stopped. the verdict reads the numbers: a jump to address 0, a run in no image (generated code of the jit), a null pointer and its offset, a full stack, a freed pointer. a fault whose pc is in no image says so in the frame
- `HANG`: a watchdog thread (`yozora.watchdog`, every half second) asks the main thread to answer and reads how long the core thread has been away from its passes. the main thread gets a block at 4, 12 and 18 s of silence (the system ends an app whose main thread is stuck for about 20 s with 0x8badf00d), the core thread at 6 and 20 s. each block has the stacks of all threads, so a loop (running, high cpu) is told from a lock (waiting, no cpu). a line `hang ended` follows when the thread answers again. a hidden app and a process that was suspended are not hangs
- `PREVIOUS RUN ENDED WITHOUT A TRACE`: a kill leaves nothing to write at. a marker file (`yozora-run.state`, mapped shared, so a plain store reaches it when the process dies a moment later) holds the state of the run, the memory sampled every half second and the last screen and note. the next launch reads it: a run that was in the foreground and neither exited nor crashed was killed. an open hang means the system watchdog, little free memory means jetsam, anything else is a kill from outside. a run that was hidden is not reported, the system ends those all the time

the recorder is installed first in `main`, before javascriptcore starts, and the notes `launch: javascriptcore start` and `launch: javascriptcore ready` bracket that step: a record with the first and not the second means the launch died while the engine started (the jit maps its executable region there). a crash before `main` (dyld, a static initializer) leaves no record, only the system crash report. the whole block is echoed to stderr, so it lands in `yozora.log` and, after the restart, in `yozora-previous.log`. after the engine starts the record gets a line `jit: …` with the tiers that are on, whether the dfg compiles concurrently and the size of the code pool.

the handler lets a fault that the hardware raised happen again: it restores the default action and returns, so the same instruction faults again on the same thread and the system report names that thread and that pc. it used to call `raise()`, which sends the signal to the process, the main thread took it and the system report of ios 8 blamed the idle main thread (stopped in `mach_msg`) while the fault was in the core thread, at address 0 in generated code. `tools/yozora_ips.py` now prints a thread that sits at no image even when the report names another.

limits: a thread other than main has no alternate stack, so a stack overflow there leaves no block. the stack walk follows frame pointers (r7 on armv7, x29 on arm64), a function without a frame is skipped. system libraries are named by dladdr (exported names only). the functions of the app are local, the block gives `Yozora +offset` counted from the load address, and `tools/yozora_record.py RECORD` names them from the map of the build, found by the uuid on the binary line (`build/symbols/UUID.map`, see below). the link writes `build/<slice>/Yozora.map`, which names every function including the local ones that `strip -x` removes from the binary, keep it with the ipa.

the system crash report of a build is read with `tools/yozora_ips.py REPORT.ips [build/symbols]`. every link of a slice keeps its map under the uuid of the binary (`scripts/archive_symbols.py`, the `symbols-armv7` and `symbols-arm64` steps, `build/symbols/UUID.map`), the report names its build by `slice_uuid`, and the tool prints the functions of the crashed thread, the local ones included. a report of a build whose map was not kept says so and lists the maps that exist: the 1.3.0 reports of 2026-11-22 and 2026-11-23 could not be read for that reason, the binary of that build has no local symbols.

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
