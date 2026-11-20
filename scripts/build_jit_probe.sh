#!/usr/bin/env bash
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_tc="${YOZORA_TC:?run through meow, which sets YOZORA_TC}"
yozora_sdk_v7="${YOZORA_SDK_V7:?set YOZORA_SDK_V7 to the legacy armv7 sdk}"
yozora_sdk_v64="${YOZORA_SDK_V64:?set YOZORA_SDK_V64 to the patched arm64 sdk}"
yozora_lipo="${YOZORA_LIPO:-${yozora_tc}/lipo}"
yozora_otool="${YOZORA_OTOOL:-${yozora_tc}/otool}"
yozora_stage="${YOZORA_JIT_STAGE:-${yozora_root}/.build-slices/jit-probe}"
yozora_output="${YOZORA_JIT_OUTPUT:-${yozora_root}/build/ios-jit-probe}"

for yozora_tool in "${yozora_tc}/clang" "${yozora_lipo}" "${yozora_otool}"; do
    [[ -x "${yozora_tool}" ]] || { echo "yozora: missing tool ${yozora_tool}" >&2; exit 1; }
done
for yozora_sdk in "${yozora_sdk_v7}" "${yozora_sdk_v64}"; do
    [[ -d "${yozora_sdk}/usr/include" ]] || { echo "yozora: missing sdk headers in ${yozora_sdk}" >&2; exit 1; }
done
if [[ -n "${YOZORA_CRT_V7:-}" && ! -f "${YOZORA_CRT_V7}" ]]; then
    echo "yozora: missing YOZORA_CRT_V7" >&2
    exit 1
fi

rm -rf "${yozora_stage}" "${yozora_output}"
mkdir -p "${yozora_stage}" "${yozora_output}"
for yozora_arch in armv7 arm64; do
    yozora_dir="${yozora_stage}/${yozora_arch}"
    mkdir -p "${yozora_dir}"
    yozora_link_extra=()
    if [[ "${yozora_arch}" == armv7 ]]; then
        yozora_sdk="${yozora_sdk_v7}"
        yozora_target=arm-apple-darwin11
        yozora_min=5.0
        # the ios 5 entry point comes from crt1.3.1.o, the default crt1.o needs LC_MAIN from ios 6
        if [[ -n "${YOZORA_CRT_V7:-}" ]]; then
            mkdir -p "${yozora_dir}/crt"
            cp "${YOZORA_CRT_V7}" "${yozora_dir}/crt/crt1.3.1.o"
            yozora_link_extra+=("-L${yozora_dir}/crt")
        fi
    else
        yozora_sdk="${yozora_sdk_v64}"
        yozora_target=arm64-apple-darwin
        yozora_min=7.0
    fi
    "${yozora_tc}/clang" -target "${yozora_target}" -B "${yozora_tc}" -arch "${yozora_arch}" \
        "-miphoneos-version-min=${yozora_min}" -isysroot "${yozora_sdk}" -std=c99 -Wall -Wextra -Werror -O2 \
        "${yozora_root}/tools/yozora_jit_probe.c" "${yozora_link_extra[@]}" -o "${yozora_dir}/yozora-jit-probe"
done
"${yozora_lipo}" -create "${yozora_stage}/armv7/yozora-jit-probe" "${yozora_stage}/arm64/yozora-jit-probe" -output "${yozora_output}/yozora-jit-probe"
cp "${yozora_root}/tools/yozora-jit.entitlements" "${yozora_output}/yozora-jit.entitlements"
"${yozora_lipo}" -info "${yozora_output}/yozora-jit-probe"
"${yozora_lipo}" "${yozora_output}/yozora-jit-probe" -verify_arch armv7 arm64
python3 -B "${yozora_root}/scripts/verify_ios.py" "${yozora_otool}" "${yozora_output}/yozora-jit-probe" --system-only
