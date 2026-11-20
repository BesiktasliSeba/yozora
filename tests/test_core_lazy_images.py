from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import zlib

from test_core_loading import read_png

ROOT = Path(__file__).resolve().parent.parent


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


PNG = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(b"\x00\xff\x00\x00\xff")) + chunk(b"IEND", b""))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *arguments):
        pass

    def do_GET(self):
        if self.path == "/feed":
            body = ("<!doctype html><meta name=viewport content='width=device-width'>"
                    "<style>body{margin:0}img{width:80px;height:100px}#sent{height:5px}</style>"
                    "<div id=container></div><div id=sent></div><script>var n=0;function add(){"
                    "for(var i=0;i<40;i++){var x=document.createElement('img');x.src='/'+n+'.png';"
                    "n++;container.appendChild(x)}}add();new IntersectionObserver(function(es){"
                    "es.forEach(function(e){if(e.isIntersecting)add()})},{rootMargin:'300px'}).observe(sent)</script>").encode()
            kind = "text/html"
        elif self.path in ("/", "/script"):
            scripted = self.path == "/script"
            attribute = "data-src" if scripted else "loading=lazy src"
            body = ("<!doctype html><meta name=viewport content='width=device-width'>"
                    "<style>body{margin:0}img{width:80px;height:100px}</style>"
                    + "".join(f"<img {attribute}='/{i}.png'>" for i in range(200))
                    + ("<script>var o=new IntersectionObserver(function(es){es.forEach(function(e){"
                       "if(e.isIntersecting){e.target.src=e.target.dataset.src;o.unobserve(e.target)}"
                       "})},{rootMargin:'400px'});document.querySelectorAll('img').forEach(function(x){o.observe(x)})</script>"
                       if scripted else "")).encode()
            kind = "text/html"
        else:
            body, kind = PNG, "image/png"
            self.server.requested.add(self.path)
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "max-age=3600")
        self.end_headers()
        self.wfile.write(body)


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.requested = set()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix="yozora-lazy-images-") as directory:
            output = Path(directory) / "page.png"
            env = dict(os.environ, YOZORA_RENDER_SCROLL="30,150", YOZORA_RENDER_WAIT_MS="2000", YOZORA_DEVICE_MEMORY_MB="512")
            cache = Path(directory) / "cache"
            cache.mkdir(mode=0o700)
            env["YOZORA_RENDER_CACHE"] = str(cache)
            for route in ("/", "/script"):
                for attempt in range(2):
                    server.requested.clear()
                    result = subprocess.run([sys.argv[1], str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                                             f"http://127.0.0.1:{server.server_port}{route}", str(output), "320", "400"],
                                            env=env, capture_output=True, text=True, timeout=120)
                    assert result.returncode == 0, result.stderr
                    if route == "/" and not attempt:
                        assert "/180.png" in server.requested, "images near the end were not requested"
                    width, height, rows = read_png(output)
                    assert (width, height) == (320, 400)
                    pixel = tuple(rows[50][80:84])
                    assert pixel == (255, 0, 0, 255), (route, attempt, "loaded images were not painted", pixel)
            server.requested.clear()
            env["YOZORA_RENDER_SCROLL"] = "60,150"
            result = subprocess.run([sys.argv[1], str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                                     f"http://127.0.0.1:{server.server_port}/feed", str(output), "320", "400"],
                                    env=env, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, result.stderr
            assert "/360.png" in server.requested, "the feed stopped adding images"
            _, _, rows = read_png(output)
            assert tuple(rows[50][80:84]) == (255, 0, 0, 255), "the appended images were not painted"
            if len(sys.argv) > 2:
                result = subprocess.run([sys.argv[2], str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                                         f"http://127.0.0.1:{server.server_port}/script"],
                                        env=env, capture_output=True, text=True, timeout=120)
                assert result.returncode == 0, result.stderr
    finally:
        server.shutdown()
        server.server_close()
    print("core images: deferred images and an infinite feed paint after scrolling, with cold and warm cache")


if __name__ == "__main__":
    main()
