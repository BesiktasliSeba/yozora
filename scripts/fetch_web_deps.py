import json
import os
from pathlib import Path
import tarfile
import urllib.request

from fetch_core import ROOT, HTTPSRedirectHandler, extract, verify_archive


def fetch_all():
    dependencies = json.loads((ROOT / "core/web-deps.json").read_text())
    cache = Path(os.environ.get("YOZORA_WEB_CACHE", ROOT / "build/cache/webcore")).resolve()
    sources = Path(os.environ.get("YOZORA_CORE_SOURCES", ROOT / "build/sources")).resolve()
    cache.mkdir(parents=True, exist_ok=True)
    sources.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(HTTPSRedirectHandler())
    result = {}
    for name, item in dependencies.items():
        archive = cache / item["archive"]
        if not archive.exists():
            temporary = archive.with_suffix(".partial")
            try:
                request = urllib.request.Request(item["url"], headers={"Accept-Encoding": "identity"})
                with opener.open(request, timeout=60) as response, temporary.open("wb") as output:
                    if response.status != 200:
                        raise RuntimeError(f"{name}: unexpected HTTP status {response.status}")
                    remaining = item["size"]
                    while remaining:
                        data = response.read(min(65536, remaining))
                        if not data:
                            raise RuntimeError(f"{name}: truncated archive")
                        output.write(data)
                        remaining -= len(data)
                    if response.read(1):
                        raise RuntimeError(f"{name}: archive exceeds pinned size")
                verify_archive(temporary, item)
                temporary.replace(archive)
            finally:
                temporary.unlink(missing_ok=True)
        verify_archive(archive, item)
        result[name] = extract(archive, item, sources)
        print(f"yozora: verified {item['archive']}", flush=True)
    return result


if __name__ == "__main__":
    try:
        fetch_all()
    except (OSError, RuntimeError, ValueError, tarfile.TarError) as error:
        raise SystemExit(f"yozora: {error}") from error
