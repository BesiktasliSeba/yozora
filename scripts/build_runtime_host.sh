#!/usr/bin/env bash
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_sources="${YOZORA_CORE_SOURCES:-${yozora_root}/build/sources}"
yozora_deps="${YOZORA_CORE_DEPS:-${yozora_root}/build/core/deps}"
yozora_llvm="${YOZORA_LLVM_SRC:-${yozora_sources}/llvm-project-23.1.1}"
yozora_icu="${YOZORA_ICU_SRC:-${yozora_sources}/icu-78.3/icu}"
yozora_jobs="${YOZORA_CORE_JOBS:-2}"
yozora_cc="$(command -v "${YOZORA_CORE_CC:-clang}")"
yozora_cxx="$(command -v "${YOZORA_CORE_CXX:-clang++}")"
[[ "${yozora_jobs}" =~ ^[1-8]$ ]] || { echo "yozora: YOZORA_CORE_JOBS must be between 1 and 8" >&2; exit 1; }
python3 -B "${yozora_root}/scripts/fetch_core_deps.py"
python3 -B "${yozora_root}/scripts/apply_core_patches.py" "${yozora_llvm}" llvm-ios
mkdir -p "${yozora_deps}/host"
yozora_deps="$(cd "${yozora_deps}" && pwd)"
yozora_runtime="${yozora_deps}/host/runtime"
yozora_build="${yozora_deps}/host/runtime-build"
cmake -S "${yozora_llvm}/runtimes" -B "${yozora_build}" -G Ninja \
    -DCMAKE_C_COMPILER="${yozora_cc}" -DCMAKE_CXX_COMPILER="${yozora_cxx}" \
    -DCMAKE_PROJECT_INCLUDE="${yozora_root}/core/Runtime.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${yozora_runtime}" \
    '-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi' \
    -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF \
    -DLIBCXXABI_USE_LLVM_UNWINDER=OFF -DLIBCXX_ABI_NAMESPACE=__yozora1 \
    -DLIBCXX_HERMETIC_STATIC_LIBRARY=ON -DLIBCXXABI_HERMETIC_STATIC_LIBRARY=ON \
    -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF
cmake --build "${yozora_build}" --target install-cxx install-cxxabi --parallel "${yozora_jobs}"
yozora_icu_build="${yozora_deps}/host/icu-build"
mkdir -p "${yozora_icu_build}"
if [[ ! -f "${yozora_icu_build}/Makefile" ]]; then
    (
        cd "${yozora_icu_build}"
        "${yozora_icu}/source/configure" --prefix="${yozora_deps}/host/icu" \
            --disable-shared --enable-static --disable-tests --disable-samples --with-data-packaging=static
    )
fi
make -C "${yozora_icu_build}" -j"${yozora_jobs}"
make -C "${yozora_icu_build}" install
"${yozora_cxx}" -std=c++23 -nostdinc++ -isystem "${yozora_runtime}/include/c++/v1" -nostdlib++ \
    -DU_STATIC_IMPLEMENTATION -I"${yozora_deps}/host/icu/include" -Wall -Wextra -Werror \
    "${yozora_root}/tools/yozora_runtime_probe.cpp" \
    "${yozora_deps}/host/icu/lib/libicui18n.a" "${yozora_deps}/host/icu/lib/libicuuc.a" \
    "${yozora_deps}/host/icu/lib/libicudata.a" \
    "${yozora_runtime}/lib/libc++.a" "${yozora_runtime}/lib/libc++abi.a" \
    -lstdc++ -ldl -pthread -o "${yozora_deps}/host/yozora-runtime-probe"
python3 -B "${yozora_root}/tests/test_runtime_probe.py" "${yozora_deps}/host/yozora-runtime-probe"
