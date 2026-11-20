import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import tempfile
import urllib.request

from fetch_core import ROOT, HTTPSRedirectHandler


def fetch_sources(manifest_path, patch_name="webkit-ios"):
    manifest = json.loads(manifest_path.read_text())
    source = json.loads((ROOT / "core/source.json").read_text())
    if manifest["upstream_commit"] != source["upstream_commit"]:
        raise RuntimeError("supplemental inputs do not match the WebKit revision")
    sources = Path(os.environ.get("YOZORA_CORE_SOURCES", ROOT / "build/sources"))
    destination = Path(os.environ.get("YOZORA_WEBKIT_SRC", sources / source["directory"])).resolve()
    if not destination.is_dir():
        raise RuntimeError("fetch WebKit before its supplemental inputs")
    groups = json.loads((ROOT / "core/patches/sources.json").read_text())
    # a tree that already carries later patch groups holds a file in any state those groups leave it, not only
    # in the state of the named group
    patched = {}
    for name, items in groups.items():
        if patch_name is None or name == patch_name:
            for item in items:
                patched.setdefault(item["path"], set()).add(item["after"])
    jobs = int(os.environ.get("YOZORA_FETCH_JOBS", "4"))
    if not 1 <= jobs <= 8:
        raise RuntimeError("YOZORA_FETCH_JOBS must be between 1 and 8")
    opener = urllib.request.build_opener(HTTPSRedirectHandler())

    def fetch(item):
        path = (destination / item["path"]).resolve()
        if not path.is_relative_to(destination) or not item["path"].startswith("Source/"):
            raise RuntimeError("supplemental source path escapes the checkout")
        if path.exists():
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual != item["sha256"] and actual not in patched.get(item["path"], ()):
                raise RuntimeError(f"modified supplemental source: {path}")
            return
        url = f"https://raw.githubusercontent.com/WebKit/WebKit/{manifest['upstream_commit']}/{item['path']}"
        with opener.open(url, timeout=60) as response:
            data = response.read(item["size"] + 1)
        if len(data) != item["size"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
            raise RuntimeError(f"supplemental input checksum mismatch: {item['path']}")
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        try:
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
        print(f"core: verified {item['path']}", flush=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(fetch, manifest["files"]))


def main():
    fetch_sources(ROOT / "core/darwin-sources.json")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(f"yozora: {error}") from error
