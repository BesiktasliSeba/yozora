from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
ROWS = "".join(f"<div style='height:20px;background:rgb({(n * 37) % 256},{(n * 91) % 256},{(n * 53) % 256})'></div>" for n in range(60))
# a 200 point panel with 1200 points of content, a second panel inside it and a 3000 point page around both.
# the title shows where the panels are after the movement
PAGE = ("<!doctype html><meta charset=utf-8><title>start</title><body style='margin:0'>"
        "<div id=outer style='position:absolute;left:20px;top:100px;width:240px;height:200px;overflow:auto;background:#ccc'>"
        f"<div style='height:100px'></div><div id=inner style='margin:0 10px;width:200px;height:80px;overflow:auto'>{ROWS}</div>{ROWS}</div>"
        "<div style='height:3000px'></div>"
        "<script>function show(){document.title='outer='+document.getElementById('outer').scrollTop+' inner='+document.getElementById('inner').scrollTop+' page='+Math.round(window.scrollY)}"
        "setInterval(show,40)</script>").encode()


# a wide table in a sideways scrolling wrapper in front of 150 other scrolling blocks below the screen. blocks
# come out of the core in no order and a limit of 32 dropped the wrapper at random, so the swipe on a Google
# answer table went to the page and the table was cut at the edge
TABLE = ("<!doctype html><meta charset=utf-8><title>start</title><body style='margin:0'>"
         "<div id=wrapper style='margin:20px 16px;overflow-x:auto'><table style='border-collapse:collapse;width:800px'><tr>"
         "<td style='width:300px;border:1px solid'>a</td><td style='width:250px;border:1px solid'>b</td><td style='width:250px;border:1px solid'>c</td></tr></table></div>"
         + "".join(f"<div style='position:absolute;top:{600 + n * 14}px;left:0;height:12px;width:60px;overflow:auto;white-space:nowrap'>{'x' * 200}</div>" for n in range(150))
         + "<div style='height:2000px'></div>"
         "<script>setInterval(function(){document.title='sideways='+document.getElementById('wrapper').scrollLeft},40)</script>").encode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        body = TABLE if self.path == "/table" else PAGE
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def run(binary, url, directory, drags):
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "inner.png"), "320", "480"],
                            capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_INNER=drags, YOZORA_RENDER_SETTLE_MS="800"))
    assert result.returncode == 0, result
    taken = re.findall(r"inner scroll (\d+),(\d+) by (-?\d+),(-?\d+) taken (\d+) rest (-?\d+),(-?\d+) page moved (-?\d+)", result.stderr)
    titles = re.findall(r"yozora: \[\s*\d+ ms\] (outer=\S+ inner=\S+ page=\S+)", result.stderr)
    assert titles, result.stderr
    return [tuple(int(value) for value in row) for row in taken], titles[-1]


def run_table(binary, url, directory):
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url + "table", str(Path(directory) / "table.png"), "320", "480"],
                            capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_INNER="100,40,200,0", YOZORA_RENDER_SETTLE_MS="800"))
    assert result.returncode == 0, result
    titles = re.findall(r"yozora: \[\s*\d+ ms\] (sideways=\S+)", result.stderr)
    assert titles, result.stderr
    return titles[-1]


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    with tempfile.TemporaryDirectory(prefix="yozora-inner-") as directory:
        # over the outer panel but not over the inner one: the outer takes 150
        taken, title = run(binary, url, directory, "30,120,0,150")
        assert taken == [(30, 120, 0, 150, 1, 0, 0, 0)] and title == "outer=150 inner=0 page=0", (taken, title)
        # over the inner panel the inner takes it first, 80 points tall with 1200 of content
        taken, title = run(binary, url, directory, "100,230,0,300")
        assert taken[0][4:] == (1, 0, 0, 0) and title.startswith("outer=0 inner=300 "), (taken, title)
        # a drag past the end of the inner panel goes to the outer one, and what it cannot take scrolls the page
        taken, title = run(binary, url, directory, "100,230,0,5000;100,230,0,5000;100,230,0,5000")
        assert taken[0][4] == 1 and taken[0][6] > 0, taken
        assert "inner=1120" in title, title
        # a point over nothing scrolls the page
        taken, title = run(binary, url, directory, "300,450,0,200")
        assert taken == [(300, 450, 0, 200, 0, 0, 200, 200)] and title.endswith("page=200"), (taken, title)
        # up again from the end
        taken, title = run(binary, url, directory, "30,120,0,150;30,120,0,-100")
        assert title == "outer=50 inner=0 page=0", title
        assert run_table(binary, url, directory) == "sideways=200", "the wrapper of a wide table did not scroll sideways among many blocks"
    server.shutdown()
    print("core inner: blocks of the page scroll under the finger before the page, the smallest first, the page takes the rest")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_inner.py yozora-render")
    main()
