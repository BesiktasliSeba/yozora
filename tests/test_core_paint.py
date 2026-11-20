from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_core_loading import ROOT, read_png

# a square box and a rounded one, each with a hard black shadow 30 px to the lower right
PAGE = b"""<!doctype html><body style="margin:0;background:#fff">
<div style="position:absolute;left:40px;top:40px;width:100px;height:60px;background:#f00;box-shadow:30px 30px 10px #000"></div>
<div style="position:absolute;left:40px;top:200px;width:100px;height:60px;background:#f00;border-radius:12px;box-shadow:30px 30px 10px #000"></div>"""


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


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="yozora-paint-") as directory:
        output = Path(directory) / "page.png"
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{server.server_address[1]}/", str(output), "320", "400"],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result
        width, height, rows = read_png(output)
    server.shutdown()
    pixel = lambda x, y: bytes(rows[y][x * 4:x * 4 + 4])
    # inside the shadow, below and right of each box, the page is nearly black. the shadow is drawn by the
    # cached mask blur path of the shadow change in webkit-yozora.patch, which once read a dropped temporary and drew nothing
    for top in (40, 200):
        assert max(pixel(160, top + 75)[:3]) < 40, (top, pixel(160, top + 75))
        assert pixel(90, top + 30) == bytes([255, 0, 0, 255]), (top, pixel(90, top + 30))
        assert pixel(250, top + 30) == bytes([255, 255, 255, 255]), (top, pixel(250, top + 30))
    print("core paint: box shadows of square and rounded boxes are drawn")


if __name__ == "__main__":
    main()
