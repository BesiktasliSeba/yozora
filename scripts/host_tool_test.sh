#!/usr/bin/env bash
# builds one host tool with the host compiler, then runs a python test against it: scripts/host_tool_test.sh TOOL TEST.py
# the tool builds the way the Makefile built them: c99 with every warning an error, no sanitizer for the jit probe
# because it jumps into a page it wrote and the instrumented run faults before the stub
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_cc="${HOST_CC:-cc}"
yozora_out="${yozora_root}/build/host"
yozora_flags=(-std=c99 -Wall -Wextra -Werror -pedantic -O2 -I"${yozora_root}/core")
mkdir -p "${yozora_out}"
cd "${yozora_root}"
case "${1:?tool}" in
    store)
        yozora_core=(core/yozora_types.c core/yozora_navigation.c core/yozora_store.c)
        "${yozora_cc}" "${yozora_flags[@]}" tools/yozora_store_cli.c "${yozora_core[@]}" -lsqlite3 -o "${yozora_out}/yozora-store"
        python3 -B "${2:?test}" "${yozora_out}/yozora-store"
        ;;
    jit-probe)
        "${yozora_cc}" "${yozora_flags[@]}" tools/yozora_jit_probe.c -o "${yozora_out}/yozora-jit-probe"
        python3 -B "${2:?test}" "${yozora_out}/yozora-jit-probe"
        ;;
    proxy)
        yozora_curl_cflags="$(pkg-config --cflags libcurl)"
        yozora_curl_libs="$(pkg-config --libs libcurl)"
        # shellcheck disable=SC2086
        "${yozora_cc}" "${yozora_flags[@]}" ${yozora_curl_cflags} tools/yozora_proxy_cli.c core/yozora_stream_proxy.c core/yozora_playlist.c core/yozora_mp4.c core/yozora_ts.c core/yozora_hls.c ${yozora_curl_libs} -lpthread -o "${yozora_out}/yozora-proxy-cli"
        # shellcheck disable=SC2086
        "${yozora_cc}" "${yozora_flags[@]}" ${yozora_curl_cflags} tests/test_proxy_local.c core/yozora_stream_proxy.c core/yozora_playlist.c core/yozora_mp4.c core/yozora_ts.c core/yozora_hls.c ${yozora_curl_libs} -lpthread -o "${yozora_out}/test_proxy_local"
        python3 -B "${2:?test}" "${yozora_out}/yozora-proxy-cli"
        "${yozora_out}/test_proxy_local"
        ;;
    *) echo "yozora: unknown host tool $1" >&2; exit 1 ;;
esac
