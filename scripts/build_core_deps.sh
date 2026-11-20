#!/usr/bin/env bash
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_sources="${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}"
yozora_deps="${YOZORA_CORE_DEPS:-${yozora_root}/build/core/deps}"
yozora_jobs="${YOZORA_CORE_JOBS:-2}"
export YOZORA_TC="${YOZORA_TC:?run through meow, which sets YOZORA_TC}"
export YOZORA_CORE_CC="$(command -v "${YOZORA_CORE_CC:-clang}")"
export YOZORA_CORE_CXX="$(command -v "${YOZORA_CORE_CXX:-clang++}")"
export YOZORA_CORE_AR="$(command -v "${YOZORA_CORE_AR:-llvm-ar}")"
export YOZORA_CORE_RANLIB="$(command -v "${YOZORA_CORE_RANLIB:-llvm-ranlib}")"
yozora_sdk_v7="${YOZORA_SDK_V7:?set YOZORA_SDK_V7}"
yozora_sdk_v64="${YOZORA_SDK_V64:?set YOZORA_SDK_V64}"
[[ "${yozora_jobs}" =~ ^[1-8]$ ]] || { echo "yozora: YOZORA_CORE_JOBS must be between 1 and 8" >&2; exit 1; }
[[ -x "${YOZORA_TC}/ld" ]] || { echo "yozora: missing Darwin linker at ${YOZORA_TC}" >&2; exit 1; }
for yozora_sdk in "${yozora_sdk_v7}" "${yozora_sdk_v64}"; do
    [[ -f "${yozora_sdk}/usr/include/mach/mach.h" ]] || { echo "yozora: missing Mach headers in ${yozora_sdk}" >&2; exit 1; }
done
python3 -B "${yozora_root}/scripts/fetch_core_deps.py"
yozora_llvm="${YOZORA_LLVM_SRC:-${yozora_sources}/llvm-project-23.1.1}"
yozora_icu="${YOZORA_ICU_SRC:-${yozora_sources}/icu-78.3/icu}"
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_llvm}" llvm-ios
mkdir -p "${yozora_deps}/host/icu-build" "${yozora_deps}/sdk"
yozora_deps="$(cd "${yozora_deps}" && pwd)"
yozora_host_icu="${yozora_deps}/host/icu-build"
if [[ ! -f "${yozora_host_icu}/Makefile" ]]; then
    (
        cd "${yozora_host_icu}"
        "${yozora_icu}/source/configure" --prefix="${yozora_deps}/host/icu" \
            --disable-shared --enable-static --disable-tests --disable-samples --with-data-packaging=static
    )
fi
make -C "${yozora_host_icu}" -j"${yozora_jobs}"
make -C "${yozora_host_icu}" install

for YOZORA_SLICE in armv7 arm64; do
    export YOZORA_SLICE
    if [[ "${YOZORA_SLICE}" == armv7 ]]; then
        yozora_sdk="${yozora_sdk_v7}"
        export YOZORA_SLICE_MIN=5.0
    else
        yozora_sdk="${yozora_sdk_v64}"
        export YOZORA_SLICE_MIN=7.0
    fi
    yozora_sdk="$(cd "${yozora_sdk}" && pwd)"
    export YOZORA_SLICE_SDK="${yozora_deps}/sdk/iPhoneOS-${YOZORA_SLICE}.sdk"
    if [[ -e "${YOZORA_SLICE_SDK}" || -L "${YOZORA_SLICE_SDK}" ]]; then
        [[ "$(readlink -f "${YOZORA_SLICE_SDK}")" == "${yozora_sdk}" ]] || {
            echo "yozora: SDK changed for ${YOZORA_SLICE}, use a fresh YOZORA_CORE_DEPS directory" >&2; exit 1;
        }
    else
        ln -s "${yozora_sdk}" "${YOZORA_SLICE_SDK}"
    fi
    export YOZORA_SLICE_DEPS="${yozora_deps}/${YOZORA_SLICE}"
    yozora_runtime="${YOZORA_SLICE_DEPS}/runtime"
    yozora_runtime_build="${YOZORA_SLICE_DEPS}/runtime-build"
    cmake -S "${yozora_llvm}/runtimes" -B "${yozora_runtime_build}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${yozora_root}/core/IOSToolchain.cmake" \
        -DCMAKE_PROJECT_INCLUDE="${yozora_root}/core/Runtime.cmake" \
        -DCMAKE_INSTALL_PREFIX="${yozora_runtime}" -DCMAKE_BUILD_TYPE=Release \
        '-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi' \
        -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF \
        -DLIBCXXABI_USE_LLVM_UNWINDER=OFF -DLIBCXX_ABI_NAMESPACE=__yozora1 \
        -DLIBCXX_HERMETIC_STATIC_LIBRARY=ON -DLIBCXXABI_HERMETIC_STATIC_LIBRARY=ON \
        -DLIBCXX_ENABLE_VENDOR_AVAILABILITY_ANNOTATIONS=OFF \
        -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF
    cmake --build "${yozora_runtime_build}" --target install-cxx install-cxxabi --parallel "${yozora_jobs}"
    mkdir -p "${YOZORA_SLICE_DEPS}/compiler" "${YOZORA_SLICE_DEPS}/icu-build"
    cp "${yozora_root}/scripts/ios_compiler.py" "${YOZORA_SLICE_DEPS}/compiler/cc"
    chmod 755 "${YOZORA_SLICE_DEPS}/compiler/cc"
    ln -sfn cc "${YOZORA_SLICE_DEPS}/compiler/cxx"
    python3 - "${yozora_runtime}" "${YOZORA_SLICE_DEPS}/compiler/compiler.json" <<'PY'
import json, os, pathlib, sys
runtime = pathlib.Path(sys.argv[1])
configuration = {
    "cc": os.environ["YOZORA_CORE_CC"], "cxx": os.environ["YOZORA_CORE_CXX"],
    "flags": ["-target", os.environ["YOZORA_SLICE"] + "-apple-ios" + os.environ["YOZORA_SLICE_MIN"],
              "-B", os.environ["YOZORA_TC"], "-mlinker-version=253", "-isysroot", os.environ["YOZORA_SLICE_SDK"]],
    "headers": str(runtime / "include/c++/v1"),
    "libraries": [str(runtime / "lib/libc++.a"), str(runtime / "lib/libc++abi.a")]
}
path = pathlib.Path(sys.argv[2])
if path.exists() and json.loads(path.read_text()) != configuration:
    raise SystemExit("yozora: compiler inputs changed, use a fresh YOZORA_CORE_DEPS directory")
path.write_text(json.dumps(configuration, indent=2) + "\n")
PY
    yozora_icu_build="${YOZORA_SLICE_DEPS}/icu-build"
    yozora_icu_configuration="$(python3 - "${yozora_icu}" "${yozora_host_icu}" "${YOZORA_SLICE_DEPS}" <<'PY'
import hashlib, json, pathlib, sys
icu, host, output = map(pathlib.Path, sys.argv[1:])
configuration = {
    "source": str(icu.resolve()), "host_build": str(host.resolve()),
    "configure": hashlib.sha256((icu / "source/configure").read_bytes()).hexdigest(),
    "compiler": json.loads((output / "compiler/compiler.json").read_text()),
    "options": ["static", "static-data", "no-tools", "no-extras", "no-tests", "no-samples"]
}
print(json.dumps(configuration, sort_keys=True))
PY
)"
    if [[ ! -f "${yozora_icu_build}/Makefile" || ! -f "${yozora_icu_build}/.yozora-config.json" || \
        "$(cat "${yozora_icu_build}/.yozora-config.json")" != "${yozora_icu_configuration}" ]]; then
        (
            cd "${yozora_icu_build}"
            CC="${YOZORA_SLICE_DEPS}/compiler/cc" CXX="${YOZORA_SLICE_DEPS}/compiler/cxx" \
            AR="${YOZORA_CORE_AR}" RANLIB="${YOZORA_CORE_RANLIB}" CFLAGS=-O2 CXXFLAGS=-O2 \
                "${yozora_icu}/source/configure" --prefix="${YOZORA_SLICE_DEPS}/icu" \
                --host="${YOZORA_SLICE}-apple-darwin" --with-cross-build="${yozora_host_icu}" \
                --disable-shared --enable-static --disable-tools --disable-extras \
                --disable-tests --disable-samples --with-data-packaging=static
        )
        printf '%s\n' "${yozora_icu_configuration}" > "${yozora_icu_build}/.yozora-config.json"
    fi
    make -C "${yozora_icu_build}" -j"${yozora_jobs}"
    make -C "${yozora_icu_build}" install
done
bash "${yozora_root}/scripts/build_runtime_ios.sh"
