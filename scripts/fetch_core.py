import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import tarfile
import tempfile
import urllib.request


ROOT = Path(__file__).resolve().parent.parent
CHUNK_SIZE = 512 * 1024


class HTTPSRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, msg, headers, new_url):
        if not new_url.startswith("https://"):
            raise RuntimeError("core download redirected outside HTTPS")
        return super().redirect_request(request, fp, code, msg, headers, new_url)


def verify_archive(path, source):
    if path.stat().st_size != source["size"]:
        raise RuntimeError("core archive has an unexpected size")
    with path.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != source["sha256"]:
        raise RuntimeError(f"core archive checksum mismatch: {actual}")


def download(source, cache, jobs):
    archive = cache / source["archive"]
    if archive.exists():
        verify_archive(archive, source)
        return archive
    parts = cache / (source["archive"] + ".parts")
    parts.mkdir(exist_ok=True)
    opener = urllib.request.build_opener(HTTPSRedirectHandler())
    size = source["size"]

    def fetch(index):
        start = index * CHUNK_SIZE
        end = min(size, start + CHUNK_SIZE) - 1
        path = parts / str(index)
        if path.exists() and path.stat().st_size == end - start + 1:
            return
        request = urllib.request.Request(source["url"], headers={
            "Range": f"bytes={start}-{end}", "Accept-Encoding": "identity"})
        temporary = path.with_suffix(".partial")
        try:
            with opener.open(request, timeout=60) as response:
                expected_range = f"bytes {start}-{end}/{size}"
                if response.status != 206 or response.headers.get("Content-Range") != expected_range:
                    raise RuntimeError("core server returned an invalid byte range")
                with temporary.open("wb") as output:
                    remaining = end - start + 1
                    while remaining:
                        data = response.read(min(16384, remaining))
                        if not data:
                            raise RuntimeError("core download ended before its declared length")
                        output.write(data)
                        remaining -= len(data)
                    if response.read(1):
                        raise RuntimeError("core range exceeds its declared length")
            temporary.replace(path)
            print(f"core: downloaded range {index + 1}", flush=True)
        finally:
            temporary.unlink(missing_ok=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(fetch, range((size + CHUNK_SIZE - 1) // CHUNK_SIZE)))
    temporary = archive.with_suffix(".partial")
    try:
        with temporary.open("wb") as output:
            for index in range((size + CHUNK_SIZE - 1) // CHUNK_SIZE):
                with (parts / str(index)).open("rb") as part:
                    shutil.copyfileobj(part, output)
        verify_archive(temporary, source)
        temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)
    return archive


def extract(archive, source, sources):
    destination = sources / source["directory"]
    if destination.exists():
        marker = destination / ".yozora-source-sha256"
        if not marker.is_file() or marker.read_text().strip() != source["sha256"]:
            raise RuntimeError(f"unverified source directory already exists: {destination}")
        return destination
    with tempfile.TemporaryDirectory(prefix=".extract-", dir=sources) as temporary:
        staging = Path(temporary)
        with tarfile.open(archive, "r:*") as package:
            for member in package.getmembers():
                components = Path(member.name).parts
                if not components or components[0] != source["directory"]:
                    raise RuntimeError("core archive has an unexpected root directory")
            package.extractall(staging, filter="data")
        extracted = staging / source["directory"]
        (extracted / ".yozora-source-sha256").write_text(source["sha256"] + "\n")
        extracted.rename(destination)
    return destination


def main():
    source = json.loads((ROOT / "core/source.json").read_text())
    cache = Path(os.environ.get("YOZORA_CORE_CACHE", ROOT / "build/cache")).resolve()
    sources = Path(os.environ.get("YOZORA_CORE_SOURCES", ROOT / "build/sources")).resolve()
    jobs = int(os.environ.get("YOZORA_FETCH_JOBS", "4"))
    if not 1 <= jobs <= 8:
        raise RuntimeError("YOZORA_FETCH_JOBS must be between 1 and 8")
    cache.mkdir(parents=True, exist_ok=True)
    sources.mkdir(parents=True, exist_ok=True)
    archive = download(source, cache, jobs)
    print(f"core: verified {source['archive']}", flush=True)
    print(extract(archive, source, sources), flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, tarfile.TarError) as error:
        raise SystemExit(f"yozora: {error}") from error
