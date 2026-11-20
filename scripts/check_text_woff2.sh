#!/usr/bin/env bash
set -euo pipefail

yozora_text_prefix="${1:?usage: check_text_woff2.sh TEXT_PREFIX}"
yozora_nm="${YOZORA_TEXT_NM:-llvm-nm}"
yozora_archive="${yozora_text_prefix}/lib/libfreetype.a"
[[ -f "${yozora_archive}" ]] || { echo "yozora: missing ${yozora_archive}, run meow build text-deps or meow build text-host" >&2; exit 1; }
yozora_symbols="$("${yozora_nm}" -u "${yozora_archive}")" || { echo "yozora: cannot inspect ${yozora_archive}" >&2; exit 1; }
if [[ "${yozora_symbols}" != *BrotliDecoderDecompress* ]]; then
    echo "yozora: ${yozora_archive} lacks WOFF2 support, rebuild it with meow build text-deps or meow build text-host" >&2
    exit 1
fi
