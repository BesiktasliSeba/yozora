"""the WebGL setting of the app reaches the page: with it off getContext('webgl') answers null and a page that draws with the GPU gets nothing to draw on, with the core's
default (on) it gets a context. an old phone that runs such a page can wedge its graphics driver, so the interface turns it on only when asked"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parent.parent
PAGE = (b"<!doctype html><title>g</title><script>var c=document.createElement('canvas');"
        b"console.log('webgl1='+(c.getContext('webgl')?'yes':'null'));</script>")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)


def answer(binary, options, port):
    env = dict(os.environ, YOZORA_RENDER_SETTLE_MS="1500")
    env.pop("YOZORA_RENDER_OPTIONS", None)
    if options:
        env["YOZORA_RENDER_OPTIONS"] = options
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{port}/", "/dev/null"],
                            capture_output=True, text=True, env=env, timeout=90)
    assert result.returncode == 0, result
    for line in result.stderr.splitlines():
        if "webgl1=" in line:
            return line.split("webgl1=")[1].split()[0]
    raise AssertionError(result.stderr)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    assert answer(binary, "", port) == "yes", "the default of the core is WebGL on"
    assert answer(binary, "webgl=1", port) == "yes"
    assert answer(binary, "webgl=0", port) == "null", "with the setting off a page gets no context"
    server.shutdown()
    print("core webgl option: on by default in the core, off when the setting says so")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_webgl_option.py yozora-render")
    main()
