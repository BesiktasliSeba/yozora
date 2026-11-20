#!/usr/bin/env bash
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["directory"])' "${yozora_root}/core/source.json")"
yozora_source="${YOZORA_WEBKIT_SRC:-${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}/${yozora_version}}"
yozora_build="${YOZORA_CORE_BUILD:-${yozora_root}/build/core/host}"
yozora_jobs="${YOZORA_CORE_JOBS:-2}"
yozora_gems="${YOZORA_CORE_GEMS:-${yozora_root}/build/deps/ruby}"
if [[ -d "${yozora_gems}" ]]; then
    export GEM_PATH="${yozora_gems}${GEM_PATH:+:${GEM_PATH}}"
fi
if ! yozora_ruby="$(command -v "${YOZORA_CORE_RUBY:-ruby}")"; then
    echo "yozora: missing Ruby, set YOZORA_CORE_RUBY" >&2
    exit 1
fi

[[ "${yozora_jobs}" =~ ^[1-8]$ ]] || { echo "yozora: YOZORA_CORE_JOBS must be between 1 and 8" >&2; exit 1; }
"${yozora_ruby}" -rerb -ryaml -rjson -e '' || {
    echo "yozora: Ruby needs erb, yaml and json for WebKit source generation" >&2
    exit 1
}
if [[ ! -d "${yozora_source}" && -z "${YOZORA_WEBKIT_SRC:-}" ]]; then
    python3 -B "${yozora_root}/scripts/fetch_core.py"
fi
[[ -f "${yozora_source}/Source/JavaScriptCore/API/JSContextRef.cpp" ]] || {
    echo "yozora: missing JavaScriptCore source at ${yozora_source}" >&2
    exit 1
}

cmake -S "${yozora_source}" -B "${yozora_build}" -G Ninja \
    -DPORT=JSCOnly -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${YOZORA_CORE_CC:-clang}" \
    -DCMAKE_CXX_COMPILER="${YOZORA_CORE_CXX:-clang++}" \
    -DCMAKE_CXX_FLAGS_RELEASE="${YOZORA_CORE_CXXFLAGS_RELEASE:--O2 -DNDEBUG}" \
    -DRuby_EXECUTABLE="${yozora_ruby}" \
    -DCMAKE_PROJECT_INCLUDE="${yozora_root}/core/Probe.cmake" \
    -DENABLE_STATIC_JSC=ON -DENABLE_C_LOOP=ON -DENABLE_JIT=OFF \
    -DENABLE_WEBASSEMBLY=OFF -DENABLE_SAMPLING_PROFILER=OFF \
    -DENABLE_REMOTE_INSPECTOR=OFF -DENABLE_JSC_GLIB_API=OFF \
    -DUSE_SYSTEM_MALLOC=ON -DEVENT_LOOP_TYPE=Generic -DUSE_HEADER_MAPS=OFF \
    -DENABLE_UNIFIED_BUILDS=ON -DWEBKIT_MAX_BUNDLE_SIZE=8 -DENABLE_API_TESTS=OFF
cmake --build "${yozora_build}" --target yozora-js yozora-js-lifecycle --parallel "${yozora_jobs}"
python3 -B "${yozora_root}/tests/test_js_cli.py" "${yozora_build}/bin/yozora-js"
"${yozora_build}/bin/yozora-js-lifecycle"
