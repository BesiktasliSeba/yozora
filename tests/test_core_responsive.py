from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
# a tall page whose script holds the page thread for 1.5 seconds after a tap at the top left corner
PAGE = ("<!doctype html><meta charset=utf-8><body style='margin:0'>"
        + "".join(f"<p style='margin:4px 8px;font:14px sans-serif'>line {n}</p>" for n in range(300))
        + "<div style='position:fixed;left:0;top:0;width:100px;height:30px;background:#ccc' onclick='console.log(\"responsive-busy-start\");var t=Date.now();while(Date.now()-t<1500);console.log(\"responsive-busy-end\");'>busy</div></body>").encode()


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
    with tempfile.TemporaryDirectory(prefix="yozora-responsive-") as directory:
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{server.server_address[1]}/", str(Path(directory) / "page.png")],
                                capture_output=True, text=True, timeout=120, env=dict(os.environ, YOZORA_RENDER_RESPONSIVE="1"))
    server.shutdown()
    assert result.returncode == 0, result
    call = re.search(r"responsive longest call (\d+) us, first picture at step (\d+) of 150, scroll requested (\d+)", result.stderr)
    painted = re.search(r"tiles cover rows (\d+) to (\d+)", result.stderr)
    assert call and painted, result.stderr
    script = re.search(r"responsive script started (\d+) ended at step (-?\d+), (\d+) pictures while busy", result.stderr)
    assert script and int(script[1]) == 1 and int(script[2]) >= 60, result.stderr
    assert int(script[3]) > 0, "the raster worker did not publish while JavaScript held WebCore"
    longest_ms, frames, requested = int(call.group(1)) / 1000, int(call.group(2)), int(call.group(3))
    covered_top, covered_bottom = int(painted.group(1)), int(painted.group(2))
    # the worker consumes an immutable recording while the page thread is still in the click handler
    assert 0 <= frames < 60, frames
    # the script blocks the page for 1500 ms, no call of the interface may take a visible part of that
    assert longest_ms < 40, longest_ms
    # 150 steps of 4 points were taken while the page was busy
    assert requested == 600, requested
    # and the page catches up: the tiles cover the view at the requested position (the default view is 400 points high)
    assert covered_top <= requested and covered_bottom >= requested + 400, (covered_top, covered_bottom, requested)
    print(f"core responsive: longest interface call {longest_ms:.1f} ms during a 1500 ms script, scroll {requested} followed")


if __name__ == "__main__":
    main()
