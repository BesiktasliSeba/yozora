from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

from test_core_loading import ROOT, read_png

PAGES = {
    "/geometry": """<div style="position:absolute;left:0;top:0;transform:translate(30px,40px);opacity:.5;
        width:100px;height:100px;overflow:hidden;background:red">
        <div style="position:absolute;left:40px;top:0;width:100px;height:100px;background:blue"></div></div>""",
    "/mask": """<div style="position:absolute;left:30px;top:40px;width:100px;height:100px;background:#00ff00;
        transform:translateZ(0);mask-image:linear-gradient(to right,black 50%,transparent 50%)"></div>""",
    "/blend": """<div style="position:absolute;left:30px;top:40px;width:100px;height:100px;background:blue;transform:translateZ(0)"></div>
        <div style="position:absolute;left:60px;top:40px;width:100px;height:100px;background:red;
        transform:translateZ(0);mix-blend-mode:multiply"></div>""",
    "/reflection": """<div style="position:absolute;left:30px;top:40px;width:50px;height:30px;background:red;
        transform:translateX(10px);will-change:opacity;-webkit-box-reflect:below 10px"></div>""",
    "/animation": """<style>@keyframes fade { from { opacity:.25 } to { opacity:.75 } }</style>
        <div style="position:absolute;left:30px;top:40px;width:100px;height:100px;background:#00ff00;animation:fade .2s linear forwards"></div>""",
    "/translation": """<style>@keyframes slide { from { transform:translateX(30px) } to { transform:translateX(80px) } }</style>
        <div style="position:absolute;left:0;top:50px;width:40px;height:30px;background:red;animation:slide .2s linear forwards"></div>""",
    "/infinite": """<style>@keyframes slide { from { transform:translateX(30px) } to { transform:translateX(80px) } }</style>
        <div style="position:absolute;left:0;top:50px;width:40px;height:30px;background:red;animation:slide 3600s linear infinite alternate"></div>""",
    "/scroll": """<div style="position:fixed;left:0;top:0;width:320px;height:20px;background:red;z-index:5">fixed</div>
        <div style="height:80px"></div><div style="height:1600px">
        <div style="position:sticky;top:20px;height:20px;background:blue;z-index:4">sticky</div></div>""",
}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *arguments):
        pass

    def do_GET(self):
        body = ("<!doctype html><meta charset=utf-8><body style='margin:0;background:white'>" + PAGES[self.path]).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix="yozora-compositor-") as directory:
            for route in PAGES:
                output = Path(directory) / (route[1:] + ".png")
                environment = dict(os.environ)
                if route in ("/animation", "/translation", "/infinite"):
                    environment["YOZORA_TRACE_RENDERING"] = "1"
                if route == "/scroll":
                    environment.update(YOZORA_RENDER_SCROLL="20,20", YOZORA_RENDER_SCROLL_FRAME_MS="16", YOZORA_TRACE_RENDERING="1")
                result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                    f"http://127.0.0.1:{server.server_port}{route}", str(output), "320", "400"],
                    env=environment, capture_output=True, text=True, timeout=90)
                assert result.returncode == 0, result
                width, height, rows = read_png(output)
                assert (width, height) == (320, 400)
                def pixel(x, y):
                    return tuple(rows[y][x * 4:x * 4 + 3])
                def color(x, y, expected):
                    actual = pixel(x, y)
                    assert all(abs(a - b) <= 2 for a, b in zip(actual, expected)), (route, x, y, actual, expected, result.stderr)
                if route == "/geometry":
                    color(40, 50, (255, 127, 127))
                    color(80, 50, (127, 127, 255))
                    color(140, 50, (255, 255, 255))
                elif route == "/mask":
                    color(40, 50, (0, 255, 0))
                    color(95, 50, (255, 255, 255))
                elif route == "/blend":
                    color(40, 50, (0, 0, 255))
                    color(80, 50, (0, 0, 0))
                    color(140, 50, (255, 0, 0))
                elif route == "/reflection":
                    color(45, 50, (255, 0, 0))
                    color(45, 75, (255, 255, 255))
                    color(45, 90, (255, 0, 0))
                elif route == "/animation":
                    assert "animations=1" in result.stderr, result.stderr
                    color(40, 50, (64, 255, 64))
                elif route == "/translation":
                    assert "animations=1" in result.stderr, result.stderr
                    color(90, 60, (255, 0, 0))
                    color(40, 60, (255, 255, 255))
                elif route == "/infinite":
                    assert "animations=1" in result.stderr, result.stderr
                    color(40, 60, (255, 0, 0))
                    color(90, 60, (255, 255, 255))
                else:
                    assert "sticky=1" in result.stderr, result.stderr
                    color(300, 10, (255, 0, 0))
                    color(300, 30, (0, 0, 255))
                    color(300, 60, (255, 255, 255))
    finally:
        server.shutdown()
        server.server_close()
    print("core compositor: transforms, opacity, masks, blending, reflections, fixed and sticky positions passed")


if __name__ == "__main__":
    main()
