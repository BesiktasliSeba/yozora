from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("loading", ROOT / "tests/test_core_loading.py")
loading = importlib.util.module_from_spec(spec)
spec.loader.exec_module(loading)

BLUE = "body{margin:0;background:#0000ff}"
log = []


def page(link):
    return f"<!doctype html><meta charset=utf-8><link rel=stylesheet href='{link}'><p>cache test</p>".encode()


# path: (content type, body, Cache-Control, etag)
ROUTES = {
    "/fresh": ("text/html", page("/fresh.css"), "max-age=3600", None),
    "/fresh.css": ("text/css", BLUE.encode(), "max-age=3600", None),
    "/validate": ("text/html", page("/validate.css"), "no-cache", '"page-1"'),
    "/validate.css": ("text/css", BLUE.encode(), "no-cache", '"css-1"'),
    "/refused": ("text/html", page("/nostore.css"), "no-cache", '"refused-1"'),
    "/nostore.css": ("text/css", BLUE.encode(), "no-store", None),
    "/a": ("text/html", page("/a.css"), "no-cache", '"a-1"'),
    "/a.css": ("text/css", BLUE.encode(), "no-cache", '"a-css-1"'),
    "/b": ("text/html", b"<!doctype html><p>second</p>", "no-store", None),
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        route = ROUTES.get(self.path)
        log.append((self.path, self.headers.get("If-None-Match")))
        if not route:
            self.send_error(404)
            return
        kind, body, control, etag = route
        if etag and self.headers.get("If-None-Match") == etag:
            self.send_response(304)
            self.send_header("ETag", etag)
            self.send_header("Cache-Control", control)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", control)
        if etag:
            self.send_header("ETag", etag)
        self.end_headers()
        self.wfile.write(body)


def render(binary, cache, port, path, directory, back_via=None):
    output = Path(directory) / "page.png"
    environment = dict(os.environ, YOZORA_LOG_REQUESTS="1", YOZORA_RENDER_CACHE=str(cache))
    if back_via:
        environment["YOZORA_RENDER_BACK_VIA"] = f"http://127.0.0.1:{port}{back_via}"
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{port}{path}", str(output)],
                            capture_output=True, text=True, timeout=90, env=environment)
    assert result.returncode == 0, result
    _, _, rows = loading.read_png(output)
    # the stylesheet made the page blue, whether it came from the network or the disk
    assert tuple(rows[300][100:104]) == (0, 0, 255, 255), (path, tuple(rows[300][100:104]))
    return result.stderr


def seen(path):
    return [entry for entry in log if entry[0] == path]


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    with tempfile.TemporaryDirectory(prefix="yozora-cache-") as directory:
        def fresh_cache(name):
            cache = Path(directory) / name
            cache.mkdir(mode=0o700)
            return cache

        # a response with a freshness lifetime is not asked for again, not even by a new process
        cache = fresh_cache("fresh")
        render(binary, cache, port, "/fresh", directory)
        assert len(seen("/fresh")) == 1 and len(seen("/fresh.css")) == 1, log
        errors = render(binary, cache, port, "/fresh", directory)
        assert len(seen("/fresh")) == 1 and len(seen("/fresh.css")) == 1, log
        assert errors.count("net: cache hit") == 2, errors

        # no-cache with a validator: the server is asked, it answers 304 and the stored body is used
        cache = fresh_cache("validate")
        render(binary, cache, port, "/validate", directory)
        assert [entry[1] for entry in seen("/validate")] == [None], log
        errors = render(binary, cache, port, "/validate", directory)
        assert [entry[1] for entry in seen("/validate")] == [None, '"page-1"'], log
        assert [entry[1] for entry in seen("/validate.css")] == [None, '"css-1"'], log
        assert errors.count("net: cache validated") == 2, errors

        # no-store is never kept
        cache = fresh_cache("refused")
        render(binary, cache, port, "/refused", directory)
        render(binary, cache, port, "/refused", directory)
        assert len(seen("/nostore.css")) == 2, log
        assert [entry[1] for entry in seen("/refused")] == [None, '"refused-1"'], log

        # back shows the stored copy even though it is stale, without a request
        cache = fresh_cache("history")
        errors = render(binary, cache, port, "/a", directory, back_via="/b")
        assert len(seen("/a")) == 1 and len(seen("/a.css")) == 1, log
        # webcore asks about the copies it holds, the stored ones answer with 304
        for path in ("/a", "/a.css"):
            assert "net: cache not modified  http://127.0.0.1:%d%s\n" % (port, path) in errors, errors

    server.shutdown()
    print("core cache: fresh copies reused, stale ones validated, no-store refused, back served from disk")


if __name__ == "__main__":
    main()
