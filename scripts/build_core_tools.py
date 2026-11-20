import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

from fetch_core import ROOT, download, extract


def main():
    metadata = json.loads((ROOT / "core/build-tools.json").read_text())["gperf"]
    cache = Path(os.environ.get("YOZORA_CORE_CACHE", ROOT / "build/cache")).resolve()
    sources = Path(os.environ.get("YOZORA_CORE_SOURCES", ROOT / "build/sources")).resolve()
    prefix = Path(os.environ.get("YOZORA_CORE_TOOLS", ROOT / "build/core/deps/host/tools")).resolve()
    build = Path(os.environ.get("YOZORA_TOOLS_BUILD", ROOT / "build/core/tools")).resolve() / "gperf"
    jobs = int(os.environ.get("YOZORA_CORE_JOBS", "2"))
    fetch_jobs = int(os.environ.get("YOZORA_FETCH_JOBS", "4"))
    if not 1 <= jobs <= 8 or not 1 <= fetch_jobs <= 8:
        raise RuntimeError("build and fetch jobs must be between 1 and 8")
    environment = os.environ.copy()
    for variable, override, default in [("CC", "YOZORA_TOOLS_CC", "cc"),
                                        ("CXX", "YOZORA_TOOLS_CXX", "c++")]:
        compiler = shutil.which(os.environ.get(override, default))
        if not compiler:
            raise RuntimeError(f"missing host compiler, set {override}")
        environment[variable] = compiler
    if os.environ.get("YOZORA_GPERF_SRC"):
        source = Path(os.environ["YOZORA_GPERF_SRC"]).resolve()
    else:
        cache.mkdir(parents=True, exist_ok=True)
        sources.mkdir(parents=True, exist_ok=True)
        archive = download(metadata, cache, fetch_jobs)
        source = extract(archive, metadata, sources)
    if not (source / "configure").is_file():
        raise RuntimeError(f"missing gperf source: {source}")
    build.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(source / "configure"), f"--prefix={prefix}"],
                   cwd=build, env=environment, check=True)
    subprocess.run(["make", f"-j{jobs}"], cwd=build, env=environment, check=True)
    subprocess.run(["make", "check"], cwd=build, env=environment, check=True)
    subprocess.run(["make", "install"], cwd=build, env=environment, check=True)
    result = subprocess.check_output([str(prefix / "bin/gperf"), "--version"], text=True)
    if result.splitlines()[0] != f"GNU gperf {metadata['version']}":
        raise RuntimeError(f"unexpected gperf version: {result.splitlines()[0]}")
    print(f"yozora: verified host generator {prefix / 'bin/gperf'}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, tarfile.TarError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"yozora: {error}") from error
