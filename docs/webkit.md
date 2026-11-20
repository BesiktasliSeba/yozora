# webkit

## the port

webkit 2.54.1 (`webkitgtk-2.54.1`), pinned in `core/source.json` by url, size and sha-256, with an own port named `Yozora`. no system webkit, no uiwebview, no wkwebview. webcore and javascriptcore come from one revision and run in one process, there is no gpu process and no network process.

the port is three layers:

- `core/patches`: changes to webkit itself, three patch files
- `core/webcore`: the platform files webkit asks a port for (graphics layer, raster, fonts, audio, gl context, accessibility stubs)
- `core/core`: the c api `yozora_core.h`, the core thread, the page client, cookies, storage, the loader

## patches

each patch carries the hashes of every file before and after, in `core/patches/sources.json`. `scripts/apply_core_patches.py` applies them in order and refuses a file whose hash matches neither, so a half applied tree is found at once and a patch applied twice does nothing.

three patches build on each other, in this order:

- `webkit-ios`: the darwin and jsc-only changes every other patch starts from
- `webkit-yozora`: the port, shared by the host build and both iOS slices. the host applies it after `webkit-ios`, so a host tree and an iOS tree can be the same directory
- `webkit-yozora-ios`: what only iOS needs on top: build fixes in crypto keys, coders, text controls and style, and a monotonic clock fix

`llvm-ios` is separate, it rebuilds libc++ under its own namespace. `webkit-yozora` holds, among others:

- port and build: `port`, `windows`, `embedder`, `cpu`, `mach`
- javascript: `jit`, `jit32`, `dfg32`, `ic32`, `llint`, `llint-entry`, `bytecode`
- graphics: `compositor`, `raster`, `clipping`, `shadow`, `skia-ios`, `scroll`, `render-trace`
- text and images: `fonts`, `woff`, `images`, `jxl`, `jxl32`, `jxl32-accept`
- network: `network`, `stream`, `websocket`, `crypto`
- platform features: `media`, `mse`, `webaudio`, `webgl`, `idb`, `workers`, `controls`, `input`, `navigator`, `navigation`, `page`, `dark`, `accessibility`, `console`, `memory`

`tests/test_core_patches.py` checks application, repeat, partial state and developer changes. `tests/test_webcore_patches.py` applies the three patches in order, twice, on the pinned sources, and needs the curl and darwin sources that are not in the archive: it skips when it has no unpatched copy of them, `YOZORA_PATCH_TEST_FETCH=1` fetches them. `meow build webcore-patches-test` runs the second, `meow test` the first.

## the runtime

- libc++ and libc++abi are built from llvm 23.1.1 into the inline namespace `std::__yozora1`, so the binary never meets the c++ library of the system
- icu 78.3 is built from source, the data is linked in
- openssl 3.5.8, curl 8.23.0, nghttp2 1.70.0, brotli 1.2.0, zlib 1.3.2 and zlib-ng 2.3.3, libpng 1.6.59, libjpeg-turbo 3.1.2, libwebp 1.6.0, libjxl 0.11.1, libxml2 2.15.4, sqlite 3.54.0, public suffix list 0.23.3, freetype 2.15.0, harfbuzz 14.6.0
- versions and hashes live in `core/deps.json`, `core/web-deps.json`, `core/text-deps.json`, `core/network-sources.json` and `core/darwin-sources.json`

## flags

the cmake options that shape the port: `PORT=Yozora`, `ENABLE_JIT` and `ENABLE_DFG_JIT` on, `ENABLE_FTL_JIT` off, `ENABLE_VIDEO`, `ENABLE_MEDIA_SOURCE`, `ENABLE_OFFSCREEN_CANVAS`, `ENABLE_WEBGL` (set by `YOZORA_WEBGL`), `USE_JPEGXL`, `ENABLE_STATIC_JSC`, unified builds, system malloc. remote inspector, the sampling profiler and the glib api are off. `ENABLE_WEBASSEMBLY` is forced off for 32 bit arm by javascriptcore itself, see [webassembly](webassembly.md).

## layout

- `core/core`: c api, core thread, page client, loader, cookies, storage, media, controls, geolocation, notifications, adblock
- `core/*.c`: portable c99 parts with host tests: navigation, store, address, accent, playlist, hls, mp4, ts, filter, blocklist, sitegrants, budget, zoom, motion, transfer, translate, keys, color
- `core/wasm`: the webassembly engine
- `core/webgl`: the entry points of angle over the driver
- `core/webcore`: the port files
- `app`: uikit, objective-c, manual reference counting
