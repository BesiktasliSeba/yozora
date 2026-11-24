#!/usr/bin/env bash
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_sources="${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}"
yozora_source="${YOZORA_WEBKIT_SRC:-${yozora_sources}/webkitgtk-2.54.3}"
yozora_deps="${YOZORA_CORE_DEPS:-${yozora_root}/build/core/deps}"
yozora_build="${YOZORA_CORE_IOS_BUILD:-${yozora_root}/build/core/ios}"
yozora_jobs="${YOZORA_CORE_JOBS:-2}"
yozora_gems="${YOZORA_CORE_GEMS:-${yozora_root}/build/deps/ruby}"
export YOZORA_TC="${YOZORA_TC:?run through meow, which sets YOZORA_TC}"
export YOZORA_CORE_CC="$(command -v "${YOZORA_CORE_CC:-clang}")"
export YOZORA_CORE_CXX="$(command -v "${YOZORA_CORE_CXX:-clang++}")"
export YOZORA_CORE_AR="$(command -v "${YOZORA_CORE_AR:-llvm-ar}")"
export YOZORA_CORE_RANLIB="$(command -v "${YOZORA_CORE_RANLIB:-llvm-ranlib}")"
[[ "${yozora_jobs}" =~ ^[1-8]$ ]] || { echo "yozora: YOZORA_CORE_JOBS must be between 1 and 8" >&2; exit 1; }
if [[ -d "${yozora_gems}" ]]; then
    export GEM_PATH="${yozora_gems}${GEM_PATH:+:${GEM_PATH}}"
fi
yozora_ruby="$(command -v "${YOZORA_CORE_RUBY:-ruby}")"
"${yozora_ruby}" -rerb -ryaml -rjson -e ''
python3 -B "${yozora_root}/scripts/fetch_core.py"
python3 -B "${yozora_root}/scripts/fetch_darwin_sources.py"
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_source}" webkit-ios
mkdir -p "${yozora_build}"
for YOZORA_SLICE in armv7 arm64; do
    export YOZORA_SLICE
    export YOZORA_SLICE_MIN=7.0
    [[ "${YOZORA_SLICE}" != armv7 ]] || export YOZORA_SLICE_MIN=5.0
    export YOZORA_SLICE_SDK="${yozora_deps}/sdk/iPhoneOS-${YOZORA_SLICE}.sdk"
    export YOZORA_SLICE_DEPS="${yozora_deps}/${YOZORA_SLICE}"
    yozora_runtime="${YOZORA_SLICE_DEPS}/runtime"
    yozora_icu="${YOZORA_SLICE_DEPS}/icu"
    for yozora_file in "${yozora_runtime}/lib/libc++.a" "${yozora_runtime}/lib/libc++abi.a" \
        "${yozora_icu}/lib/libicui18n.a" "${yozora_icu}/lib/libicuuc.a" "${yozora_icu}/lib/libicudata.a"; do
        [[ -f "${yozora_file}" ]] || { echo "yozora: missing ${yozora_file}, run meow build core-deps" >&2; exit 1; }
    done
    yozora_flags="-nostdinc++ -isystem \"${yozora_runtime}/include/c++/v1\" -nostdlib++ -faligned-allocation -DYOZORA_DARWIN=1 -I\"${yozora_root}/core/darwin\""
    cmake -S "${yozora_source}" -B "${yozora_build}/${YOZORA_SLICE}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${yozora_root}/core/IOSToolchain.cmake" \
        -DCMAKE_CXX_FLAGS="-B \"${YOZORA_TC}\" -mlinker-version=253 ${yozora_flags}" \
        -DCMAKE_CXX_STANDARD_LIBRARIES="\"${yozora_runtime}/lib/libc++.a\" \"${yozora_runtime}/lib/libc++abi.a\"" \
        -DCMAKE_CXX_FLAGS_RELEASE="${YOZORA_CORE_CXXFLAGS_RELEASE:--O2 -DNDEBUG}" \
        -DCMAKE_PROJECT_INCLUDE="${yozora_root}/core/Probe.cmake" \
        -DYOZORA_DARWIN_ROOT="${yozora_root}/core/darwin" -DYOZORA_ICU_ROOT="${yozora_icu}" \
        -DRuby_EXECUTABLE="${yozora_ruby}" -DPORT=JSCOnly -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_STATIC_JSC=ON -DENABLE_C_LOOP=ON -DENABLE_JIT=OFF \
        -DENABLE_WEBASSEMBLY=OFF -DENABLE_SAMPLING_PROFILER=OFF \
        -DENABLE_REMOTE_INSPECTOR=OFF -DENABLE_JSC_GLIB_API=OFF \
        -DUSE_SYSTEM_MALLOC=ON -DEVENT_LOOP_TYPE=Generic -DUSE_HEADER_MAPS=OFF \
        -DENABLE_UNIFIED_BUILDS=ON -DWEBKIT_MAX_BUNDLE_SIZE=8 -DENABLE_API_TESTS=OFF
    cmake --build "${yozora_build}/${YOZORA_SLICE}" --target yozora-js yozora-js-lifecycle --parallel "${yozora_jobs}"
done
for yozora_probe in yozora-js yozora-js-lifecycle; do
    "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" -create "${yozora_build}/armv7/bin/${yozora_probe}" \
        "${yozora_build}/arm64/bin/${yozora_probe}" -output "${yozora_build}/${yozora_probe}"
    "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" -info "${yozora_build}/${yozora_probe}"
    "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" "${yozora_build}/${yozora_probe}" -verify_arch armv7 arm64
    python3 -B "${yozora_root}/scripts/verify_core_ios.py" "${YOZORA_OTOOL:-${YOZORA_TC}/otool}" \
        "${yozora_build}/${yozora_probe}" "${YOZORA_NM:-${YOZORA_TC}/nm}"
done

for yozora_library in JavaScriptCore WTF bmalloc; do
    yozora_output="${yozora_build}/lib${yozora_library}.a"
    "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" -create "${yozora_build}/armv7/lib/lib${yozora_library}.a" \
        "${yozora_build}/arm64/lib/lib${yozora_library}.a" -output "${yozora_output}"
    "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" "${yozora_output}" -verify_arch armv7 arm64
    chmod 644 "${yozora_output}"
    "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" -info "${yozora_output}"
done
