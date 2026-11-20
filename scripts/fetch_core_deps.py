import json
import os
from pathlib import Path
import subprocess
import tempfile

from fetch_core import ROOT, download, extract


def checkout(source, sources):
    destination = sources / source["directory"]
    if not destination.exists():
        with tempfile.TemporaryDirectory(prefix=".llvm-", dir=sources) as temporary:
            staging = Path(temporary) / source["directory"]
            subprocess.run(["git", "clone", "--filter=blob:none", "--no-checkout", "--depth=1",
                            "--branch", source["tag"], source["url"], str(staging)], check=True)
            verify_checkout(source, staging)
            subprocess.run(["git", "-C", str(staging), "sparse-checkout", "init", "--cone"], check=True)
            subprocess.run(["git", "-C", str(staging), "sparse-checkout", "set", *source["paths"]], check=True)
            subprocess.run(["git", "-C", str(staging), "checkout", "--detach", source["commit"]], check=True)
            staging.rename(destination)
    verify_checkout(source, destination)
    for path in source["paths"]:
        if not (destination / path).is_dir():
            raise RuntimeError(f"LLVM checkout is missing {path}")
    return destination


def verify_checkout(source, path):
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != source["commit"]:
        raise RuntimeError(f"LLVM revision mismatch: {actual}")


def main():
    dependencies = json.loads((ROOT / "core/deps.json").read_text())
    cache = Path(os.environ.get("YOZORA_CORE_CACHE", ROOT / "build/cache")).resolve()
    sources = Path(os.environ.get("YOZORA_CORE_SOURCES", ROOT / "build/sources")).resolve()
    jobs = int(os.environ.get("YOZORA_FETCH_JOBS", "4"))
    if not 1 <= jobs <= 8:
        raise RuntimeError("YOZORA_FETCH_JOBS must be between 1 and 8")
    cache.mkdir(parents=True, exist_ok=True)
    sources.mkdir(parents=True, exist_ok=True)
    icu = dependencies["icu"]
    archive = download(icu, cache, jobs)
    icu_sources = sources / ("icu-" + icu["version"])
    icu_sources.mkdir(exist_ok=True)
    print(extract(archive, icu, icu_sources), flush=True)
    print(checkout(dependencies["llvm"], sources), flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"yozora: {error}") from error
