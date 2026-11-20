from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
BANDS = "".join(f"<div style='height:23px;background:rgb({(n * 37) % 256},{(n * 91) % 256},{(n * 53) % 256})'></div>" for n in range(400))
# a script that keeps the core thread busy most of the time, like a heavy page: tile passes come every few
# frames, so the tiles have to be there before the page gets to them
BUSY = "<script>setInterval(function(){var t=Date.now();while(Date.now()-t<45);},5)</script>"
# a timer every 20 ms keeps the longest gap between its runs and counts the runs after the first long gap
TIMERS = ("<script>var last=Date.now(),gap=0,after=0,paused=false;setInterval(function(){var now=Date.now(),g=now-last;last=now;if(g>gap)gap=g;"
          "if(g>300)paused=true;if(paused)after++;document.title='gap='+gap+' after='+after},20)</script>")
# the page scrolls itself from its timer, as a smooth scroll script or a carousel does
SELF_SCROLL = ("<script>var last=Date.now(),gap=0;setInterval(function(){var now=Date.now(),g=now-last;last=now;if(g>gap)gap=g;"
               "window.scrollBy(0,5);document.title='gap='+gap+' after=0'},20)</script>")
PAGES = {
    "/idle": "<!doctype html><meta charset=utf-8><body style='margin:0'>" + BANDS,
    "/busy": "<!doctype html><meta charset=utf-8><body style='margin:0'>" + BANDS + BUSY,
    "/selfscroll": "<!doctype html><meta charset=utf-8><body style='margin:0'>" + BANDS + SELF_SCROLL,
    "/timers": "<!doctype html><meta charset=utf-8><body style='margin:0'>" + BANDS + TIMERS,
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        body = PAGES[self.path].encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def fling(binary, url, directory, frames, step, frame_ms):
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "fling.png"), "320", "480"],
                            capture_output=True, text=True, timeout=180,
                            env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_SCROLL=f"{frames},{step}", YOZORA_RENDER_SCROLL_FRAME_MS=str(frame_ms)))
    assert result.returncode == 0, result
    gaps = re.search(r"scroll gaps (\d+) of (\d+) frames showed untiled rows, worst (\d+) points", result.stderr)
    assert gaps, result.stderr
    return int(gaps.group(1)), int(gaps.group(2)), int(gaps.group(3))


def tiles_kept(binary, url, directory, frames, step, frame_ms):
    # the rows the tiles cover once the page came to rest after a drag down, and where the view is
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "kept.png"), "320", "480"],
                            capture_output=True, text=True, timeout=180,
                            env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_SCROLL=f"{frames},{step}", YOZORA_RENDER_SCROLL_FRAME_MS=str(frame_ms)))
    assert result.returncode == 0, result
    span = re.search(r"tiles (\d+) cover rows (\d+) to (\d+), view at (\d+)", result.stderr)
    assert span, result.stderr
    return int(span.group(2)), int(span.group(4))


def timer_gap(binary, url, directory, scroll):
    environment = dict(os.environ, YOZORA_RENDER_SCALE="2")
    if scroll:
        environment.update(YOZORA_RENDER_SCROLL=scroll, YOZORA_RENDER_SCROLL_FRAME_MS="16")
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "timers.png"), "320", "480"],
                            capture_output=True, text=True, timeout=180, env=environment)
    assert result.returncode == 0, result
    found = re.findall(r"gap=(\d+) after=(\d+)", result.stderr)
    assert found, result.stderr
    return int(found[-1][0]), int(found[-1][1])


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    with tempfile.TemporaryDirectory(prefix="yozora-fling-") as directory:
        # 120 points every 16 ms is 7500 points a second, faster than a thumb flings
        idle = fling(binary, base + "/idle", directory, 40, 120, 16)
        busy = fling(binary, base + "/busy", directory, 40, 120, 16)
        slow = fling(binary, base + "/busy", directory, 40, 60, 40)
        kept = tiles_kept(binary, base + "/idle", directory, 20, 60, 33)
        resting = timer_gap(binary, base + "/timers", directory, None)
        moving = timer_gap(binary, base + "/timers", directory, "40,120")
        scripted = timer_gap(binary, base + "/selfscroll", directory, None)
    server.shutdown()
    # compositing and raster work do not suspend the page's timers during a fling
    assert resting[0] < 300, resting
    assert moving[0] < 300, moving
    # a script that scrolls the page is not the user and keeps its timer
    assert scripted[0] < 300, scripted
    # a page that is not busy keeps up with any fling
    assert idle[0] == 0, idle
    # with the thread busy, the reach of 128 points left a third of the frames white. the tiles ahead of the page keep it down
    assert busy[0] <= 6 and busy[2] <= 160, busy
    assert slow[0] == 0, slow
    # tiles behind the window stay in the pool, dragging back up showed white while they were painted again.
    # at rest the reach starts one tile above the view and the tiles kept above that are the cache
    assert kept[0] <= kept[1] - 384, kept
    print("core fling: tiles ahead of a fast fling, %d of %d frames without a tile on a busy page" % (busy[0], busy[1]))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_fling.py yozora-render")
    main()
