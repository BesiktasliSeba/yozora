import json
import os
from pathlib import Path
import tarfile

from fetch_core import ROOT, download, extract


def main():
    dependencies = json.loads((ROOT / "core/text-deps.json").read_text())
    cache = Path(os.environ.get("YOZORA_CORE_CACHE", ROOT / "build/cache")).resolve()
    sources = Path(os.environ.get("YOZORA_CORE_SOURCES", ROOT / "build/sources")).resolve()
    jobs = int(os.environ.get("YOZORA_FETCH_JOBS", "4"))
    if not 1 <= jobs <= 8:
        raise RuntimeError("YOZORA_FETCH_JOBS must be between 1 and 8")
    cache.mkdir(parents=True, exist_ok=True)
    sources.mkdir(parents=True, exist_ok=True)
    for dependency in dependencies.values():
        archive = download(dependency, cache, jobs)
        print(extract(archive, dependency, sources), flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, tarfile.TarError) as error:
        raise SystemExit(f"yozora: {error}") from error
