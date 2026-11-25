#!/usr/bin/env bash
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_mode="${1:-webcore}"
[[ "${yozora_mode}" == configure || "${yozora_mode}" == webcore || "${yozora_mode}" == graphics || "${yozora_mode}" == memory || "${yozora_mode}" == raster || "${yozora_mode}" == fonts || "${yozora_mode}" == platform || "${yozora_mode}" == page || "${yozora_mode}" == page-adapter-sanitize || "${yozora_mode}" == core ]] || { echo "yozora: expected configure, webcore, graphics, memory, raster, fonts, platform, page, page-adapter-sanitize or core" >&2; exit 1; }
if [[ "${yozora_mode}" != graphics && "$(uname -s)" != Linux ]]; then
    echo "yozora: the WebCore host probes currently require Linux" >&2
    exit 1
fi
yozora_memory_sanitize="${YOZORA_MEMORY_SANITIZE:-OFF}"
[[ "${yozora_memory_sanitize}" == ON || "${yozora_memory_sanitize}" == OFF ]] || { echo "yozora: YOZORA_MEMORY_SANITIZE must be ON or OFF" >&2; exit 1; }
yozora_version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["directory"])' "${yozora_root}/core/source.json")"
yozora_source="${YOZORA_WEBKIT_SRC:-${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}/${yozora_version}}"
yozora_build="${YOZORA_WEBCORE_BUILD:-${yozora_root}/build/core/webcore-host}"
yozora_c_flags="${YOZORA_CORE_CFLAGS_RELEASE:--O2 -DNDEBUG}"
yozora_cxx_flags="${YOZORA_CORE_CXXFLAGS_RELEASE:--O2 -DNDEBUG}"
yozora_link_flags="${YOZORA_CORE_LINK_FLAGS:-}"
if [[ "${yozora_memory_sanitize}" == ON ]]; then
    yozora_build="${yozora_build}-memory-sanitize"
    yozora_c_flags="${yozora_c_flags} -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined"
    yozora_cxx_flags="${yozora_cxx_flags} -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined"
    yozora_link_flags="${yozora_link_flags} -fsanitize=address,undefined"
fi
yozora_deps="${YOZORA_CORE_DEPS:-${yozora_root}/build/core/deps}"
yozora_tools="${YOZORA_CORE_TOOLS:-${yozora_deps}/host/tools}"
yozora_text="${YOZORA_TEXT_DEPS:-${yozora_deps}}/host/text"
yozora_icu="${yozora_deps}/host/icu"
yozora_jobs="${YOZORA_CORE_JOBS:-2}"
yozora_gems="${YOZORA_CORE_GEMS:-${yozora_root}/build/deps/ruby}"
[[ "${yozora_jobs}" =~ ^[1-8]$ ]] || { echo "yozora: YOZORA_CORE_JOBS must be between 1 and 8" >&2; exit 1; }
if [[ -d "${yozora_gems}" ]]; then
    export GEM_PATH="${yozora_gems}${GEM_PATH:+:${GEM_PATH}}"
fi
yozora_ruby="$(command -v "${YOZORA_CORE_RUBY:-ruby}")" || { echo "yozora: missing Ruby, install it or set YOZORA_CORE_RUBY" >&2; exit 1; }
"${yozora_ruby}" -rerb -ryaml -rjson -e '' || { echo "yozora: Ruby needs erb, yaml and json" >&2; exit 1; }
yozora_gperf="${YOZORA_GPERF:-${yozora_tools}/bin/gperf}"
[[ -x "${yozora_gperf}" ]] || { echo "yozora: missing host gperf, run meow build core-tools or set YOZORA_GPERF" >&2; exit 1; }
for yozora_file in "${yozora_text}/lib/libharfbuzz-icu.a" "${yozora_text}/lib/libfreetype.a" "${yozora_icu}/lib/libicuuc.a"; do
    [[ -f "${yozora_file}" ]] || { echo "yozora: missing ${yozora_file}, run meow build runtime-host and meow build text-host" >&2; exit 1; }
done
bash "${yozora_root}/scripts/check_text_woff2.sh" "${yozora_text}"
if [[ ! -d "${yozora_source}" && -z "${YOZORA_WEBKIT_SRC:-}" ]]; then
    python3 -B "${yozora_root}/scripts/fetch_core.py"
fi
python3 -B "${yozora_root}/scripts/fetch_network_sources.py"
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_source}" webkit-ios
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_source}" webkit-yozora
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_source}" webkit-yozora-dns
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_source}" webkit-yozora-gcm

cmake -S "${yozora_source}" -B "${yozora_build}" -G Ninja \
    -DPORT=Yozora -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${YOZORA_CORE_CC:-clang}" -DCMAKE_CXX_COMPILER="${YOZORA_CORE_CXX:-clang++}" \
    -DCMAKE_C_FLAGS_RELEASE="${yozora_c_flags}" -DCMAKE_CXX_FLAGS_RELEASE="${yozora_cxx_flags}" \
    -DCMAKE_EXE_LINKER_FLAGS="${yozora_link_flags}" \
    -DCMAKE_PREFIX_PATH="${yozora_text};${yozora_icu}${YOZORA_WEBCORE_PREFIX_PATH:+;${YOZORA_WEBCORE_PREFIX_PATH}}" \
    -DYOZORA_ICU_ROOT="${yozora_icu}" -DGPERF_EXECUTABLE="${yozora_gperf}" -DRuby_EXECUTABLE="${yozora_ruby}" \
    -DYOZORA_WEBCORE_ROOT="${YOZORA_WEBCORE_ROOT:-${yozora_root}/core/webcore}" \
    -DCMAKE_PROJECT_INCLUDE="${yozora_root}/core/WebCoreProbe.cmake" \
    -DENABLE_C_LOOP=ON -DENABLE_JIT=OFF -DENABLE_VIDEO=ON -DENABLE_MEDIA_SOURCE=ON -DENABLE_OFFSCREEN_CANVAS=ON -DENABLE_WEBASSEMBLY=OFF -DENABLE_SAMPLING_PROFILER=OFF \
    -DUSE_SYSTEM_MALLOC=ON -DUSE_HEADER_MAPS=OFF -DUSE_THIN_ARCHIVES=OFF \
    -DENABLE_UNIFIED_BUILDS=ON -DWEBKIT_MAX_BUNDLE_SIZE=8
if [[ "${yozora_mode}" == configure ]]; then
    exit 0
fi
if [[ "${yozora_mode}" == graphics || "${yozora_mode}" == webcore ]]; then
    cmake --build "${yozora_build}" --target yozora-skia --parallel "${yozora_jobs}"
    yozora_harfbuzz="${YOZORA_HARFBUZZ_SRC:-${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}/$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["harfbuzz"]["directory"])' "${yozora_root}/core/text-deps.json")}"
    python3 -B "${yozora_root}/tests/test_skia_probe.py" "${yozora_build}/bin/yozora-skia" \
        "${YOZORA_TEST_FONT:-${yozora_harfbuzz}/test/subset/data/fonts/Roboto-Regular.ttf}" \
        "${YOZORA_TEST_ARABIC_FONT:-${yozora_harfbuzz}/test/subset/data/fonts/Amiri-Regular.ttf}"
fi
if [[ "${yozora_mode}" == memory || "${yozora_mode}" == webcore ]]; then
    cmake --build "${yozora_build}" --target yozora-memory --parallel "${yozora_jobs}"
    python3 -B "${yozora_root}/tests/test_webcore_memory.py" "${yozora_build}/bin/yozora-memory"
fi
if [[ "${yozora_mode}" == raster || "${yozora_mode}" == webcore ]]; then
    cmake --build "${yozora_build}" --target yozora-raster yozora-raster-platform --parallel "${yozora_jobs}"
    python3 -B "${yozora_root}/tests/test_webcore_raster.py" "${yozora_build}/bin/yozora-raster"
fi
if [[ "${yozora_mode}" == fonts || "${yozora_mode}" == webcore ]]; then
    cmake --build "${yozora_build}" --target yozora-fonts yozora-font-platform --parallel "${yozora_jobs}"
    yozora_harfbuzz="${YOZORA_HARFBUZZ_SRC:-${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}/$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["harfbuzz"]["directory"])' "${yozora_root}/core/text-deps.json")}"
    python3 -B "${yozora_root}/tests/test_webcore_fonts.py" "${yozora_build}/bin/yozora-fonts" "${yozora_harfbuzz}"
fi
if [[ "${yozora_mode}" == webcore ]]; then
    cmake --build "${yozora_build}" --target WebCore WebCoreStyle WebCoreDOMAndRendering WebCoreJSBindings --parallel "${yozora_jobs}"
fi
if [[ "${yozora_mode}" == platform || "${yozora_mode}" == webcore ]]; then
    cmake --build "${yozora_build}" --target yozora-platform --parallel "${yozora_jobs}"
    "${yozora_build}/bin/yozora-platform"
fi
if [[ "${yozora_mode}" == page || "${yozora_mode}" == webcore || "${yozora_mode}" == page-adapter-sanitize ]]; then
    yozora_page_target=yozora-page
    if [[ "${yozora_mode}" == page-adapter-sanitize ]]; then
        yozora_page_target=yozora-page-adapter-sanitize
    fi
    cmake --build "${yozora_build}" --target "${yozora_page_target}" --parallel "${yozora_jobs}"
    yozora_harfbuzz="${YOZORA_HARFBUZZ_SRC:-${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}/$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["harfbuzz"]["directory"])' "${yozora_root}/core/text-deps.json")}"
    python3 -B "${yozora_root}/tests/test_webcore_page.py" "${yozora_build}/bin/${yozora_page_target}" "${yozora_harfbuzz}"
fi
if [[ "${yozora_mode}" == core ]]; then
    cmake --build "${yozora_build}" --target yozora-render yozora-scene-tests yozora-memory-tests yozora-scroll-tests yozora-lazy-scene-tests --parallel "${yozora_jobs}"
    "${yozora_build}/bin/yozora-scene-tests"
    "${yozora_build}/bin/yozora-scroll-tests"
    python3 -B "${yozora_root}/tests/test_core_memory.py" "${yozora_build}/bin/yozora-memory-tests"
    python3 -B "${yozora_root}/tests/test_core_compositor.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_render_metrics.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_loading.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_page_errors.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_gc.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_images.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_lazy_images.py" "${yozora_build}/bin/yozora-render" "${yozora_build}/bin/yozora-lazy-scene-tests"
    python3 -B "${yozora_root}/tests/test_core_cache.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_bytecode.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_runloop.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_lifecycle.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_apis.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_input.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_touch.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_longpress.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_webaudio.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_geolocation.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_media.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_frames.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_priority.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_responsive.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_scroll.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_fling.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_stop.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_inner.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_select.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_download.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_auth.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_idb.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_websocket.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_sitedata.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_tiles.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_options.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_webgl_option.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_paint.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_translate.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_wasm.py" "${yozora_build}/bin/yozora-render"
    python3 -B "${yozora_root}/tests/test_core_wasm_stop.py" "${yozora_build}/bin/yozora-render"
fi
