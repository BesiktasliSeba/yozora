import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import urllib.request


spec = importlib.util.spec_from_file_location(
    "fetch_core", Path(__file__).resolve().parent.parent / "scripts/fetch_core.py")
fetch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch)


def reject(function, *arguments):
    try:
        function(*arguments)
    except (RuntimeError, tarfile.FilterError):
        return
    raise AssertionError("unsafe source input was accepted")


def fixture(root, members):
    archive = root / "fixture.tar.xz"
    with tarfile.open(archive, "w:xz") as package:
        for name, text, target in members:
            entry = tarfile.TarInfo(name)
            if target:
                entry.type = tarfile.SYMTYPE
                entry.linkname = target
                package.addfile(entry)
            else:
                entry.size = len(text)
                package.addfile(entry, io.BytesIO(text))
    source = {"archive": archive.name, "directory": "fixture", "size": archive.stat().st_size,
              "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
    return archive, source


def main():
    with tempfile.TemporaryDirectory(prefix="yozora-source-") as temporary:
        root = Path(temporary)
        sources = root / "sources"
        sources.mkdir()
        archive, source = fixture(root, [("fixture/Source/core.c", b"core\n", None)])
        fetch.verify_archive(archive, source)
        assert fetch.download(source, root, 1) == archive
        destination = fetch.extract(archive, source, sources)
        assert (destination / "Source/core.c").read_bytes() == b"core\n"
        assert fetch.extract(archive, source, sources) == destination
        (destination / ".yozora-source-sha256").unlink()
        reject(fetch.extract, archive, source, sources)
        assert (destination / "Source/core.c").read_bytes() == b"core\n"
        reject(fetch.verify_archive, archive, dict(source, sha256="0" * 64))
        reject(fetch.verify_archive, archive, dict(source, size=source["size"] + 1))
        archive.write_bytes(b"broken cache")
        reject(fetch.download, source, root, 1)

        unsafe = [
            [("outside/core.c", b"escaped", None)],
            [("fixture/../../outside", b"escaped", None)],
            [("fixture/link", b"", "../../outside")],
            [("fixture/link", b"", str(root / "outside"))],
        ]
        for index, members in enumerate(unsafe):
            case = root / str(index)
            case.mkdir()
            output = case / "sources"
            output.mkdir()
            archive, source = fixture(case, members)
            reject(fetch.extract, archive, source, output)
            assert not (output / "fixture").exists()
            assert not list(output.glob(".extract-*"))
        assert not (root / "outside").exists()
        request = urllib.request.Request("https://example.org/source")
        handler = fetch.HTTPSRedirectHandler()
        reject(handler.redirect_request, request, None, 302, "Found", {}, "http://example.org/source")
    print("core source: checksum, size, cache integrity, extraction bounds and HTTPS redirects passed")


if __name__ == "__main__":
    main()
