#!/usr/bin/env bash
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_mode="${1:?usage: build_text_deps.sh host|ios}"
[[ "${yozora_mode}" == host || "${yozora_mode}" == ios ]] || { echo "yozora: expected host or ios" >&2; exit 1; }
yozora_deps="${YOZORA_CORE_DEPS:-${yozora_root}/build/core/deps}"
yozora_text_deps="${YOZORA_TEXT_DEPS:-${yozora_deps}}"
yozora_web_deps="${YOZORA_WEB_DEPS:-${yozora_deps}}"
yozora_build="${YOZORA_TEXT_BUILD:-${yozora_root}/build/core/text}"
yozora_sources="${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}"
yozora_jobs="${YOZORA_CORE_JOBS:-2}"
[[ "${yozora_jobs}" =~ ^[1-8]$ ]] || { echo "yozora: YOZORA_CORE_JOBS must be between 1 and 8" >&2; exit 1; }
export YOZORA_CORE_CC="$(command -v "${YOZORA_CORE_CC:-clang}")"
export YOZORA_CORE_CXX="$(command -v "${YOZORA_CORE_CXX:-clang++}")"
export YOZORA_CORE_AR="$(command -v "${YOZORA_CORE_AR:-llvm-ar}")"
export YOZORA_CORE_RANLIB="$(command -v "${YOZORA_CORE_RANLIB:-llvm-ranlib}")"
python3 -B "${yozora_root}/scripts/fetch_text_deps.py"
yozora_freetype="${YOZORA_FREETYPE_SRC:-${yozora_sources}/$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["freetype"]["directory"])' "${yozora_root}/core/text-deps.json")}"
yozora_harfbuzz="${YOZORA_HARFBUZZ_SRC:-${yozora_sources}/$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["harfbuzz"]["directory"])' "${yozora_root}/core/text-deps.json")}"
yozora_slices=(host)
if [[ "${yozora_mode}" == ios ]]; then
    export YOZORA_TC="${YOZORA_TC:?run through meow, which sets YOZORA_TC}"
    yozora_slices=(armv7 arm64)
fi

for YOZORA_SLICE in "${yozora_slices[@]}"; do
    export YOZORA_SLICE
    export YOZORA_SLICE_DEPS="${yozora_deps}/${YOZORA_SLICE}"
    yozora_runtime="${YOZORA_SLICE_DEPS}/runtime"
    yozora_icu="${YOZORA_SLICE_DEPS}/icu"
    yozora_c_flags=""
    for yozora_file in "${yozora_runtime}/lib/libc++.a" "${yozora_runtime}/lib/libc++abi.a" \
        "${yozora_icu}/lib/libicuuc.a" "${yozora_icu}/lib/libicudata.a"; do
        [[ -f "${yozora_file}" ]] || { echo "yozora: missing ${yozora_file}, build the core runtime first" >&2; exit 1; }
    done
    yozora_flags="-nostdinc++ -isystem \"${yozora_runtime}/include/c++/v1\" -nostdlib++ -faligned-allocation"
    yozora_libraries="\"${yozora_runtime}/lib/libc++.a\" \"${yozora_runtime}/lib/libc++abi.a\""
    yozora_configuration=()
    if [[ "${YOZORA_SLICE}" == host ]]; then
        # host ICU was built with the host's C++ library
        yozora_libraries+=" -lstdc++ -ldl -pthread"
    else
        export YOZORA_SLICE_MIN=7.0
        [[ "${YOZORA_SLICE}" != armv7 ]] || export YOZORA_SLICE_MIN=5.0
        export YOZORA_SLICE_SDK="${yozora_deps}/sdk/iPhoneOS-${YOZORA_SLICE}.sdk"
        [[ -f "${YOZORA_SLICE_SDK}/usr/include/mach/mach.h" ]] || { echo "yozora: missing ${YOZORA_SLICE} SDK" >&2; exit 1; }
        yozora_c_flags="-B \"${YOZORA_TC}\" -mlinker-version=253"
        yozora_flags="${yozora_c_flags} ${yozora_flags}"
        yozora_configuration+=(-DCMAKE_TOOLCHAIN_FILE="${yozora_root}/core/IOSToolchain.cmake")
    fi
    cmake -S "${yozora_root}/core/text" -B "${yozora_build}/${YOZORA_SLICE}" -G Ninja \
        "${yozora_configuration[@]}" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="${YOZORA_CORE_CC}" -DCMAKE_CXX_COMPILER="${YOZORA_CORE_CXX}" \
        -DCMAKE_C_FLAGS="${yozora_c_flags} ${YOZORA_TEXT_CFLAGS:-}" \
        -DCMAKE_CXX_FLAGS="${yozora_flags} ${YOZORA_TEXT_CXXFLAGS:-}" \
        -DCMAKE_CXX_STANDARD_LIBRARIES="${yozora_libraries}" \
        -DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_CXX_STANDARD_LIBRARIES \
        -DCMAKE_EXE_LINKER_FLAGS="${YOZORA_TEXT_LDFLAGS:-}" \
        -DCMAKE_AR="${YOZORA_CORE_AR}" -DCMAKE_RANLIB="${YOZORA_CORE_RANLIB}" \
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_PREFIX="${yozora_text_deps}/${YOZORA_SLICE}/text" \
        -DYOZORA_FREETYPE_SRC="${yozora_freetype}" -DYOZORA_HARFBUZZ_SRC="${yozora_harfbuzz}" \
        -DYOZORA_ICU_ROOT="${yozora_icu}" -DYOZORA_WEB_ROOT="${yozora_web_deps}/${YOZORA_SLICE}/web"
    cmake --build "${yozora_build}/${YOZORA_SLICE}" --parallel "${yozora_jobs}"
    cmake --install "${yozora_build}/${YOZORA_SLICE}"
done

if [[ "${yozora_mode}" == host ]]; then
    python3 -B "${yozora_root}/tests/test_text_probe.py" "${yozora_text_deps}/host/text/bin/yozora-text" \
        "${YOZORA_TEST_FONT:-${yozora_harfbuzz}/test/subset/data/fonts/Roboto-Regular.ttf}" \
        "${YOZORA_TEST_ARABIC_FONT:-${yozora_harfbuzz}/test/subset/data/fonts/Amiri-Regular.ttf}"
else
    mkdir -p "${yozora_build}/universal"
    for yozora_library in libfreetype.a libharfbuzz.a libharfbuzz-icu.a; do
        "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" -create "${yozora_text_deps}/armv7/text/lib/${yozora_library}" \
            "${yozora_text_deps}/arm64/text/lib/${yozora_library}" -output "${yozora_build}/universal/${yozora_library}"
        chmod 644 "${yozora_build}/universal/${yozora_library}"
        "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" "${yozora_build}/universal/${yozora_library}" -verify_arch armv7 arm64
    done
    "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" -create "${yozora_text_deps}/armv7/text/bin/yozora-text" \
        "${yozora_text_deps}/arm64/text/bin/yozora-text" -output "${yozora_build}/yozora-text"
    "${YOZORA_LIPO:-${YOZORA_TC}/lipo}" "${yozora_build}/yozora-text" -verify_arch armv7 arm64
    python3 -B "${yozora_root}/scripts/verify_core_ios.py" "${YOZORA_OTOOL:-${YOZORA_TC}/otool}" \
        "${yozora_build}/yozora-text" "${YOZORA_NM:-${YOZORA_TC}/nm}"
fi
