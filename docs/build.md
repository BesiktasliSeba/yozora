# build

## host

```bash
meow test
meow build yozora-fat yozora-store-fat
```

[meow](https://github.com/sqmrak/meow), a c99 compiler, python 3.12 and sqlite headers. `meow test` builds every c test in `tests/` with `-Wall -Wextra -Werror -pedantic` under asan and ubsan and runs them with the python tests for sources, patches and scripts. everything is in `meow.toml`: `meow list` shows it, `meow plan NAME` prints the commands and what is stale, `meow why NAME` says what made a step stale.

## webkit on the host

```bash
meow build core-fetch
meow build core-host
meow build core-web-host
build/core/host/bin/yozora-js tests/js/features.js
```

cmake 3.20, ninja, c++23, icu 70.1 or newer, ruby, perl. the host build is the one that runs `yozora-render`, the headless page runner behind most of the core tests. a full webcore build takes about 25 minutes with `-j 4` and needs about 8 GB of ram.

a build tree keeps the absolute path it was configured in. after the source tree moves, configure a new tree, an old one cannot compile.

## ios

```bash
meow build            # core-fetch, core-tools, core-deps, text-deps, web-deps, WebKit, the app, both ipas
meow build ipa        # one ipa with both slices
meow build app --slice arm64
```

- meow orders the dependency steps and skips what is built: `core-deps`, `text-deps` and `web-deps` run only while their outputs are missing, `meow clean NAME` then `meow build NAME` forces one. the older probes keep their names: `core-fat`, `webcore-fat` (the cmake build of the whole port, the Yozora executable included), `runtime-fat`, `graphics-fat`, `memory-fat`
- WebKit builds through cmake and ninja in the steps `webcore-armv7` and `webcore-arm64` (`scripts/build_webcore_ios.sh` with `YOZORA_BUILD_APP=0`). cmake owns the library graph; `scripts/export_link_line.py` writes the link line it computed for the Yozora target to `build/<slice>/link.rsp`, and meow compiles the objective-c sources, links with that file, strips and signs. a change to one `app/*.m` rebuilds one object and the link
- armv7 links against a legacy sdk, arm64 against a patched sdk that has the private symbols, each slice has its own staging directory and objects are never shared between slices
- the arm64 binary carries the sdk version `YOZORA_SDK_V64_VERSION` (16.5, the headers the patched sdk was made from) through `-Wl,-sdk_version`: ld 253 reads the version from the digits of the sdk directory name, and `iPhoneOS-arm64.sdk` gave 64.0
- meow signs each slice and the joined file with `ldid` and the entitlements of `tools/yozora-jit.entitlements`, reading the signature back
- the release checks (`release-armv7`, `release-arm64`, `release-universal`) run before packaging: the slices, the deployment target, a signature, only system dylibs. the ipa is read back after it is written
- `meow build ipa-armv7` and `meow build ipa-arm64` write one slice each
- the icons, interface glyphs and launch images sit flat in the bundle and are listed one by one in the ipa tables, because meow places a tree under a name and not at the root; `tests/test_bundle_inputs.py` fails when a file is added to those folders and not listed

outputs `build/ios/libyozora.a`, `build/ios/yozora-store`, `build/ipa/Yozora-armv7.ipa`, `build/ipa/Yozora-arm64.ipa` and `build/ipa/Yozora.ipa`.

```bash
meow build deb        # build/ipa/Yozora-v1.4.0-stable.deb
```

the deb is cut from the checked universal ipa by `scripts/build_deb.sh` (`YOZORA_DEB_IPA`, `YOZORA_DEB_OUT` and `YOZORA_DEB_STAGE` move its paths). `Architecture: all`, gzip members, the bundle in `/var/jb/Applications/Yozora.app` with root ownership, files 644 and the binary 755. a rootless jailbreak registers that path with its uicache, on a rootful one `postinst` moves the bundle to `/Applications` (a copy would keep two bundles of 100 MB) and marks it with `.yozora-rootful-move`, `postrm` removes only a bundle that carries the mark. the script stops when the version of `packaging/DEBIAN/control` is not the one in the plist of the ipa.

## variables

nothing is read from a fixed path, every root can be set:

- toolchain: meow finds it (`meow doctor` lists what it found; `MEOW_TC` and `MEOW_SDK_*` set it by hand) and hands it to the scripts as `YOZORA_TC`. `YOZORA_TC` set in the environment wins, `YOZORA_CRT_V7`
- sdks: `YOZORA_SDK_V7`, `YOZORA_SDK_V64`
- compilers: `YOZORA_CORE_CC`, `YOZORA_CORE_CXX`, `YOZORA_CORE_AR`, `YOZORA_CORE_RANLIB`, flags in `YOZORA_CORE_CFLAGS_RELEASE`, `YOZORA_CORE_CXXFLAGS_RELEASE`, `YOZORA_CORE_LINK_FLAGS`
- trees: `YOZORA_CORE_BUILD`, `YOZORA_CORE_IOS_BUILD`, `YOZORA_WEBCORE_BUILD`, `YOZORA_WEBCORE_IOS_BUILD`, `YOZORA_TEXT_BUILD`, `YOZORA_WEB_DEPS_BUILD`, `YOZORA_TOOLS_BUILD`
- sources and caches: `YOZORA_WEBKIT_SRC`, `YOZORA_CORE_SOURCES`, `YOZORA_CORE_CACHE`, `YOZORA_WEB_CACHE`, `YOZORA_ICU_SRC`, `YOZORA_LLVM_SRC`, `YOZORA_FREETYPE_SRC`, `YOZORA_HARFBUZZ_SRC`, `YOZORA_SQLITE_SRC`
- jobs: `YOZORA_CORE_JOBS`, `YOZORA_FETCH_JOBS`
- the webcore tree meow links the app against: `YOZORA_WEBCORE_IOS_BUILD` (the jit tree `build/core/webcore-ios-jit` by default)
- features: `YOZORA_WASM` (keeps the webassembly of javascriptcore on arm64), `YOZORA_ARMV7_JIT=0` (builds the c loop instead of the jit)

a script that needs one of them and does not find it stops with the name.

## other targets

steps, run by name with `meow build NAME`: `runtime-host`, `text-host`, `web-deps-host`, `graphics-host`, `raster-host`, `fonts-host`, `memory-host`, `platform-host`, `page-host`, `webcore-host`, `page-adapter-sanitize`, `webcore-patches-test`, `jit-probe`. the proxy and the wasm tests are host tests: `meow test proxy`, `meow test wasm`.

## inputs

every archive is fetched once and checked by sha-256 against `core/*.json` before it is unpacked, a patch is applied only when the file has the hash recorded before it. see [webkit](webkit.md).

## emoji font

`app/fonts/AppleColorEmoji.ttf` is generated, not committed (see `app/NOTICE-Emoji.txt`). `tools/make_emoji_font.py` writes it from an emoji pack and a data table, `tests/test_emoji_font.py` checks it. the ipa build requires it. both need fonttools and pillow.

## assets

- `tools/make_icons.py SOURCE.png OUTPUT_DIR`: app icons with rounded corners
- `tools/make_home_art.py`: the cut out sticker and the wordmark mask of the start page
- `tools/make_launch_images.py`: launch images for every iphone and ipad screen and the `UILaunchImages` entries
- `tools/make_ui_icons.py`: interface glyph masks from `tools/material-symbols` and the marks of the search engines from `tools/logos`, needs `rsvg-convert` and pillow

## the c files built into the app

some of the c code is used by the app only and is compiled with it (`meow.toml`, the lists of `app`, `app-armv7`, `app-arm64`) instead of into `libYozoraCore`, which needs a webkit build:

- `core/yozora_blur.c`: the blur of the start page photo, see [core](core.md) and [ui](ui.md)
