import os
from pathlib import Path
import subprocess
import sys

from fetch_core import ROOT


def test(mode):
    if mode not in ("host", "ios"):
        raise RuntimeError("expected host or ios")
    deps = Path(os.environ.get("YOZORA_CORE_DEPS", ROOT / "build/core/deps")).resolve()
    web = Path(os.environ.get("YOZORA_WEB_DEPS", deps)).resolve()
    builds = Path(os.environ.get("YOZORA_WEB_DEPS_BUILD", ROOT / "build/core/web-deps")).resolve()
    libraries = ["png16", "jpeg", "webp", "sharpyuv", "brotlienc", "brotlidec", "brotlicommon", "xml2", "sqlite3",
                 "curl", "psl", "nghttp2", "ssl", "crypto", "z"]
    for arch in (("host",) if mode == "host" else ("armv7", "arm64")):
        prefix = web / arch / "web"
        icu = deps / arch / "icu"
        compiler = os.environ.get("YOZORA_CORE_CC", "clang") if arch == "host" else deps / arch / "compiler/cc"
        runtime = (["-lstdc++", "-ldl"] if arch == "host" else
                   [deps / arch / "runtime/lib/libc++.a", deps / arch / "runtime/lib/libc++abi.a"])
        probe = builds / arch / "yozora-web-deps"
        args = [compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(prefix / "include"),
                "-I" + str(prefix / "include/libxml2"), ROOT / "tests/test_web_dependencies.c",
                *[prefix / "lib" / f"lib{name}.a" for name in libraries],
                icu / "lib/libicuuc.a", icu / "lib/libicudata.a", *runtime, "-lpthread", "-lm", "-o", probe]
        subprocess.run(list(map(str, args)), check=True)
        if arch == "host":
            subprocess.run([probe], check=True)
    if mode == "ios":
        tc = Path(os.environ["YOZORA_TC"])
        result = builds / "yozora-web-deps"
        subprocess.run([tc / "lipo", "-create", builds / "armv7/yozora-web-deps", builds / "arm64/yozora-web-deps", "-output", result], check=True)
        subprocess.run([tc / "lipo", "-info", result], check=True)
        subprocess.run([sys.executable, "-B", ROOT / "scripts/verify_core_ios.py", tc / "otool", result, tc / "nm"], check=True)


if __name__ == "__main__":
    try:
        test(sys.argv[1] if len(sys.argv) == 2 else "")
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"yozora: {error}") from error
