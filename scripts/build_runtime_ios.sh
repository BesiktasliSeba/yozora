#!/usr/bin/env bash
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_deps="${YOZORA_CORE_DEPS:-${yozora_root}/build/core/deps}"
yozora_tc="${YOZORA_TC:?run through meow, which sets YOZORA_TC}"
yozora_lipo="${YOZORA_LIPO:-${yozora_tc}/lipo}"
yozora_otool="${YOZORA_OTOOL:-${yozora_tc}/otool}"
mkdir -p "${yozora_deps}/universal"

for yozora_slice in armv7 arm64; do
    yozora_prefix="${yozora_deps}/${yozora_slice}"
    yozora_libraries=("${yozora_prefix}/runtime/lib/libc++.a"
        "${yozora_prefix}/runtime/lib/libc++abi.a"
        "${yozora_prefix}/icu/lib/libicui18n.a"
        "${yozora_prefix}/icu/lib/libicuuc.a"
        "${yozora_prefix}/icu/lib/libicudata.a")
    yozora_link=()
    for yozora_library in "${yozora_libraries[@]}"; do
        [[ -f "${yozora_library}" ]] || { echo "yozora: missing ${yozora_library}, run meow build core-deps" >&2; exit 1; }
        "${yozora_lipo}" "${yozora_library}" -verify_arch "${yozora_slice}"
        # force every object into the probe to expose unused imports of newer ios APIs
        yozora_link+=(-Xlinker -force_load -Xlinker "${yozora_library}")
    done
    "${yozora_prefix}/compiler/cxx" -std=c++23 -O2 -Wall -Wextra -Werror \
        -DU_STATIC_IMPLEMENTATION -I"${yozora_prefix}/icu/include" -I"${yozora_root}/core/darwin" \
        "${yozora_root}/tools/yozora_runtime_probe.cpp" "${yozora_link[@]}" \
        -o "${yozora_prefix}/yozora-runtime-probe"
done

"${yozora_lipo}" -create "${yozora_deps}/armv7/yozora-runtime-probe" \
    "${yozora_deps}/arm64/yozora-runtime-probe" -output "${yozora_deps}/yozora-runtime-probe"
"${yozora_lipo}" "${yozora_deps}/yozora-runtime-probe" -verify_arch armv7 arm64
"${yozora_lipo}" -info "${yozora_deps}/yozora-runtime-probe"
python3 -B "${yozora_root}/scripts/verify_core_ios.py" "${yozora_otool}" \
    "${yozora_deps}/yozora-runtime-probe" "${YOZORA_NM:-${yozora_tc}/nm}"

for yozora_library in runtime/lib/libc++.a runtime/lib/libc++abi.a \
    icu/lib/libicui18n.a icu/lib/libicuuc.a icu/lib/libicudata.a; do
    yozora_output="${yozora_deps}/universal/$(basename "${yozora_library}")"
    "${yozora_lipo}" -create "${yozora_deps}/armv7/${yozora_library}" \
        "${yozora_deps}/arm64/${yozora_library}" -output "${yozora_output}"
    "${yozora_lipo}" "${yozora_output}" -verify_arch armv7 arm64
    chmod 644 "${yozora_output}"
    "${yozora_lipo}" -info "${yozora_output}"
done
