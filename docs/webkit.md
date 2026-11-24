# webkit

## the port

webkit 2.54.3 (`webkitgtk-2.54.3`), pinned in `core/source.json` by url, size and sha-256, with an own port named `Yozora`. no system webkit, no uiwebview, no wkwebview. webcore and javascriptcore come from one revision and run in one process, there is no gpu process and no network process.

the port is three layers:

- `core/patches`: changes to webkit itself, three patch files
- `core/webcore`: the platform files webkit asks a port for (graphics layer, raster, fonts, audio, gl context, accessibility stubs)
- `core/core`: the c api `yozora_core.h`, the core thread, the page client, cookies, storage, the loader

## patches

each patch carries the hashes of every file before and after, in `core/patches/sources.json`. `scripts/apply_core_patches.py` applies them in order and refuses a file whose hash matches neither, so a half applied tree is found at once and a patch applied twice does nothing.

three patches build on each other, in this order:

- `webkit-ios`: the darwin and jsc-only changes every other patch starts from, and the script disallowed count of the dom without thread local storage (see below)
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

## webkit 2.54.3

the pin moved from 2.54.1 (`core/source.json`, `core/darwin-sources.json`, `core/network-sources.json`, `core/patches/sources.json`, the version strings of the app and the docs). what the move needed in the sources of the port:

- `Source/WebCore/dom/ScriptDisallowedScope.h` and `ContainerNode.cpp` (group `webkit-ios`): 2.54.2 made the counter of the script disallowed scopes `thread_local` so that every thread counts for itself. armv7 on ios before 9 has no thread local storage and clang stops with `thread-local storage is not supported for the current target`. the counter is a plain static again, only the main thread counts (as in 2.54.1: the constructor and destructor of `ScriptDisallowedScope` return on another thread) and `isScriptAllowedInCurrentThread()` answers as `isScriptAllowedInMainThread()`, so a worker is always allowed. the other `thread_local` of the tree (`StackBounds`, `MallocCommon`, `DOMWrapperWorld`) are in the tarball of 2.54.1 as well
- `Source/WebCore/platform/graphics/skia/SkiaUtilities.cpp` and `.h` (group `webkit-yozora`): the gpu parts of these files stay inside `#if USE(SKIA_GPU)`. 2.54.3 added `scaleImagePixels` (a cpu function: `SkBitmap`, `SkImages::RasterFromBitmap`) after `createPromiseImageIfNeeded`, so it sits outside the condition and the includes it needs (`IntSize.h`, `skia/core/SkBitmap.h`) are outside too
- `core/webcore/SystemFontDatabaseYozora.cpp`: 2.54.3 defines `SystemFontDatabase::singleton()` itself for a platform that is not cocoa (it answers `FontCache::forCurrentThread().systemFontDatabase()`, one database per thread), so the port keeps only `platformSystemFontShorthandInfo` and `platformInvalidate`. the old definition would be a second one in the archive
- the hashes of 2 files of `webkit-ios` (`Source/WTF/wtf/CMakeLists.txt`, `PlatformJSCOnly.cmake`) and 13 of `webkit-yozora` (`DFGSpeculativeJIT.cpp`, `LowLevelInterpreter32_64.asm`, `LowLevelInterpreter64.asm`, `JSCJSValue.h`, `skia/CMakeLists.txt`, `Document.cpp`, `ChromeClient.h`, `FontCache.h`, `ImageBuffer.cpp`, `MediaPlayer.h`, `SkiaUtilities.cpp`, `SkiaUtilities.h`, `RenderLayerCompositor.cpp`) moved: the hunks apply with an offset of a few lines. the 90 files fetched from the WebKit repository did not change
- for arm64 the web audio filters need `___divdc3`, see [build](build.md)

how the move was made and how to make the next one is in [build](build.md).

## the runtime

- libc++ and libc++abi are built from llvm 23.1.1 into the inline namespace `std::__yozora1`, so the binary never meets the c++ library of the system
- icu 78.3 is built from source, the data is linked in
- openssl 3.5.8, curl 8.23.0, nghttp2 1.70.0, brotli 1.2.0, zlib 1.3.2 and zlib-ng 2.3.3, libpng 1.6.59, libjpeg-turbo 3.1.2, libwebp 1.6.0, libjxl 0.11.1, libxml2 2.15.4, sqlite 3.54.0, public suffix list 0.23.3, freetype 2.15.0, harfbuzz 14.6.0
- versions and hashes live in `core/deps.json`, `core/web-deps.json`, `core/text-deps.json`, `core/network-sources.json` and `core/darwin-sources.json`

## flags

the cmake options that shape the port: `PORT=Yozora`, `ENABLE_JIT` and `ENABLE_DFG_JIT` on, `ENABLE_FTL_JIT` off, `ENABLE_VIDEO`, `ENABLE_MEDIA_SOURCE`, `ENABLE_OFFSCREEN_CANVAS`, `ENABLE_WEBGL` (set by `YOZORA_WEBGL`), `USE_JPEGXL`, `ENABLE_STATIC_JSC`, unified builds, system malloc. bundles of cold directories of WebCore are built with `-Os`, see [build](build.md). remote inspector, the sampling profiler and the glib api are off. `ENABLE_WEBASSEMBLY` is forced off for 32 bit arm by javascriptcore itself, see [webassembly](webassembly.md).

## layout

- `core/core`: c api, core thread, page client, loader, cookies, storage, media, controls, geolocation, notifications, adblock
- `core/*.c`: portable c99 parts with host tests: navigation, store, address, accent, playlist, hls, mp4, ts, filter, blocklist, sitegrants, budget, zoom, motion, transfer, translate, keys, color
- `core/wasm`: the webassembly engine
- `core/webgl`: the entry points of angle over the driver
- `core/webcore`: the port files
- `app`: uikit, objective-c, manual reference counting
