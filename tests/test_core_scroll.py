from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
# every band has its own colour and number, so a strip painted at the wrong row shows up as a different picture
PAGE = ("<!doctype html><meta charset=utf-8><body style='margin:0'>"
        + "".join(f"<div style='height:23px;background:rgb({(n * 37) % 256},{(n * 91) % 256},{(n * 53) % 256});font:12px sans-serif;color:#fff'>band {n}</div>"
                  for n in range(200))
        + "</body>").encode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)


def render(binary, url, directory, name, scroll):
    output = Path(directory) / name
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(output), "320", "400"],
                            capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, YOZORA_RENDER_OVERSCAN="64", YOZORA_RENDER_SCALE="2", YOZORA_RENDER_SCROLL=scroll))
    assert result.returncode == 0, result
    paints = int(re.search(r"in (\d+) paints, picture", result.stderr).group(1))
    return output.read_bytes(), paints


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    with tempfile.TemporaryDirectory(prefix="yozora-scroll-") as directory:
        # 600 points in 30 drags paint the tiles that come into reach one by one, a single drag of 600 paints them in one pass
        strips, strip_paints = render(binary, url, directory, "strips.png", "30,20")
        whole, _ = render(binary, url, directory, "whole.png", "1,600")
        # dragging back up brings tiles into reach at the top
        up, _ = render(binary, url, directory, "up.png", "60,20;30,-20")
    server.shutdown()
    # the pictures are compared as encoded files: the same pixels give the same bytes
    assert strips == whole, "tiles painted while dragging differ from tiles painted in one pass"
    assert up == whole, "tiles painted while dragging up differ from tiles painted in one pass"
    # a tile is painted when it comes into reach, 600 points are at most six tiles. the scrollbar thumb used
    # to invalidate the tiles under it on every step, which painted thirty times
    assert strip_paints <= 10, strip_paints
    print("core scroll: tiles painted while dragging, down and back up, equal tiles painted in one pass")


if __name__ == "__main__":
    main()
