# measures the screen rows that had no tile while a scroll ran, on a host made as slow as a phone with the
# core stand variables (YOZORA_RASTER_TILE_DELAY_US, YOZORA_RECORD_DELAY_MS_PER_1000). not part of meow test,
# prints a table to compare two builds
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parent.parent
BANDS = "".join(f"<div style='color:#fff;height:23px;background:rgb({(n * 37) % 200},{(n * 91) % 200},{(n * 53) % 200})'>{n} text of a long page with more words<img src='data:image/gif;base64,R0lGODlhAQABAAAAACw=' width=60 height=20></div>" for n in range(900))
PAGE = ("<!doctype html><meta charset=utf-8><body style='margin:0'>" + BANDS).encode()
# a drag is STEPS frames of DY points: a slow read, a fling, a fling that turns back, and a long fling
SCENARIOS = {
    "read 40x6": "40,6",
    "fling 40x40": "40,40",
    "fling 60x80": "60,80",
    "turn 30x60;30x-60": "30,60;30,-60",
    "long 120x60": "120,60",
}


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
    tile_us = sys.argv[2] if len(sys.argv) > 2 else "4000"
    record_ms = sys.argv[3] if len(sys.argv) > 3 else "120"
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print(f"tile {tile_us} us, recording {record_ms} ms per 1000 points, 2 raster threads, 16 ms per frame")
    with tempfile.TemporaryDirectory(prefix="yozora-gaps-") as directory:
        for name, drags in SCENARIOS.items():
            def run(trace):
                # the trace of the core slows the passes, so the recordings are counted by a run of their own
                environment = dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_SCROLL=drags, YOZORA_RENDER_SCROLL_FRAME_MS="16",
                                   YOZORA_RASTER_TILE_DELAY_US=tile_us, YOZORA_RECORD_DELAY_MS_PER_1000=record_ms, YOZORA_RENDER_BLANK="1")
                if trace:
                    environment["YOZORA_TRACE_RENDERING"] = "1"
                return subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "gaps.png"), "320", "480"],
                                      capture_output=True, text=True, timeout=300, env=environment)
            result = run(False)
            gaps = re.search(r"scroll gaps (\d+) of (\d+) frames showed untiled rows, worst (\d+) points", result.stderr)
            versions = {}
            for m in re.finditer(r"yozora-scene: id=(\d+) .*?version=(\d+)", run(True).stderr):
                versions[m.group(1)] = max(versions.get(m.group(1), 0), int(m.group(2)))
            records = max(versions.values() or [0])
            if not gaps:
                print(f"{name:22} no result: {result.stderr[-200:]}")
                continue
            print(f"{name:22} frames with a gap {gaps.group(1):>3} of {gaps.group(2):>3}, worst {gaps.group(3):>4} points, recordings {records}")
    server.shutdown()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: measure_scroll_gaps.py yozora-render [tile_delay_us] [record_ms_per_1000]")
    main()
