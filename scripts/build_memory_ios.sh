#!/usr/bin/env bash
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_tc="${YOZORA_TC:?run through meow, which sets YOZORA_TC}"
yozora_deps="${YOZORA_CORE_DEPS:-${yozora_root}/build/core/deps}"
yozora_core="${YOZORA_CORE_IOS_BUILD:-${yozora_root}/build/core/ios}"
yozora_build="${YOZORA_MEMORY_IOS_BUILD:-${yozora_root}/build/core/memory-ios}"
yozora_source="${YOZORA_WEBKIT_SRC:-${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}/webkitgtk-2.54.3}"
yozora_cc="${YOZORA_CORE_CC:-clang}"
yozora_cxx="${YOZORA_CORE_CXX:-clang++}"
yozora_lipo="${YOZORA_LIPO:-${yozora_tc}/lipo}"
yozora_otool="${YOZORA_OTOOL:-${yozora_tc}/otool}"
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_source}" webkit-ios
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_source}" webkit-yozora
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_source}" webkit-yozora-dns
mkdir -p "${yozora_build}"
for yozora_arch in armv7 arm64; do
    yozora_slice="${yozora_build}/${yozora_arch}"
    yozora_sdk="${yozora_deps}/sdk/iPhoneOS-${yozora_arch}.sdk"
    yozora_runtime="${yozora_deps}/${yozora_arch}/runtime"
    yozora_icu="${yozora_deps}/${yozora_arch}/icu"
    yozora_core_slice="${yozora_core}/${yozora_arch}"
    for yozora_input in "${yozora_sdk}/usr/include/mach/mach.h" "${yozora_core_slice}/cmakeconfig.h" \
        "${yozora_core_slice}/lib/libWTF.a" "${yozora_runtime}/lib/libc++.a"; do
        [[ -f "${yozora_input}" ]] || { echo "yozora: missing ${yozora_input}, build core-fat first" >&2; exit 1; }
    done
    mkdir -p "${yozora_slice}"
    yozora_min=7.0
    [[ "${yozora_arch}" != armv7 ]] || yozora_min=5.0
    yozora_flags=(-target "${yozora_arch}-apple-ios${yozora_min}" -B "${yozora_tc}" -mlinker-version=253
        -isysroot "${yozora_sdk}" -O2 -Wall -Wextra -Werror -I"${yozora_root}/core/darwin")
    "${yozora_cc}" "${yozora_flags[@]}" -std=c99 -c "${yozora_root}/core/darwin/yozora_memory.c" -o "${yozora_slice}/yozora_memory.o"
    "${yozora_cc}" "${yozora_flags[@]}" -std=c99 -UNDEBUG "${yozora_root}/tests/test_memory_darwin.c" \
        "${yozora_slice}/yozora_memory.o" -o "${yozora_slice}/yozora-mach-memory"
    yozora_cxx_flags=("${yozora_flags[@]}" -std=c++23 -DNDEBUG -DHAVE_CONFIG_H=1 -DBUILDING_YOZORA__=1 -DBUILDING_WITH_CMAKE=1
        -DSTATICALLY_LINKED_WITH_WTF -DSTATICALLY_LINKED_WITH_bmalloc -DSTATICALLY_LINKED_WITH_JavaScriptCore
        -DYOZORA_DARWIN=1 -DPAS_BMALLOC=1 -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
        -DU_STATIC_IMPLEMENTATION -fno-strict-aliasing -fno-exceptions -fno-rtti -fvisibility=hidden -fvisibility-inlines-hidden
        -nostdinc++ -isystem "${yozora_runtime}/include/c++/v1" -nostdlib++ -faligned-allocation
        -I"${yozora_source}/Source/WebCore" -I"${yozora_source}/Source/WebCore/PAL" -I"${yozora_source}/Source/WTF"
        -I"${yozora_core_slice}" -I"${yozora_core_slice}/WTF/Headers"
        -I"${yozora_core_slice}/bmalloc/Headers" -I"${yozora_core_slice}/JavaScriptCore/Headers"
        -I"${yozora_core_slice}/JavaScriptCore/PrivateHeaders" -I"${yozora_icu}/include"
        -I"${yozora_source}/Source/WebCore" -I"${yozora_source}/Source/WebCore/platform")
    yozora_objects=("${yozora_slice}/yozora_memory.o")
    yozora_inputs=("${yozora_root}/core/darwin/MachSendRightYozora.cpp"
        "${yozora_root}/core/webcore/SharedMemoryDarwin.cpp"
        "${yozora_source}/Source/WebCore/platform/SharedMemory.cpp"
        "${yozora_source}/Source/WebCore/platform/SharedBuffer.cpp"
        "${yozora_root}/tests/test_webcore_memory_darwin.cpp")
    for yozora_index in "${!yozora_inputs[@]}"; do
        yozora_object="${yozora_slice}/memory-${yozora_index}.o"
        "${yozora_cxx}" "${yozora_cxx_flags[@]}" -c "${yozora_inputs[yozora_index]}" -o "${yozora_object}"
        yozora_objects+=("${yozora_object}")
    done
    yozora_frameworks=(-framework CoreFoundation)
    if [[ "${yozora_arch}" == arm64 ]]; then
        yozora_frameworks+=(-framework Foundation -lobjc)
    fi
    "${yozora_cxx}" "${yozora_cxx_flags[@]}" "${yozora_objects[@]}" -Wl,-dead_strip \
        "${yozora_core_slice}/lib/libWTF.a" "${yozora_core_slice}/lib/libJavaScriptCore.a" \
        "${yozora_core_slice}/lib/libbmalloc.a" "${yozora_icu}/lib/libicui18n.a" \
        "${yozora_icu}/lib/libicuuc.a" "${yozora_icu}/lib/libicudata.a" \
        "${yozora_runtime}/lib/libc++.a" "${yozora_runtime}/lib/libc++abi.a" \
        "${yozora_frameworks[@]}" -o "${yozora_slice}/yozora-webcore-memory"
done
for yozora_probe in yozora-mach-memory yozora-webcore-memory; do
    "${yozora_lipo}" -create "${yozora_build}/armv7/${yozora_probe}" "${yozora_build}/arm64/${yozora_probe}" -output "${yozora_build}/${yozora_probe}"
    "${yozora_lipo}" "${yozora_build}/${yozora_probe}" -verify_arch armv7 arm64
    "${yozora_lipo}" -info "${yozora_build}/${yozora_probe}"
    python3 -B "${yozora_root}/scripts/verify_core_ios.py" "${yozora_otool}" "${yozora_build}/${yozora_probe}" "${YOZORA_NM:-${yozora_tc}/nm}"
done
