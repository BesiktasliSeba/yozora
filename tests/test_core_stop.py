from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
# the first timer never returns, the second must run once the script was stopped and so must a third later,
# which a request left pending for the next script would end
PAGE = ("<!doctype html><meta charset=utf-8><title>start</title><body>text<script>"
        "setTimeout(function(){document.title='spinning';for(;;){}},100);"
        "setTimeout(function(){document.title='alive'},400);"
        "setTimeout(function(){document.title='alive again'},1200);</script>").encode()


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
    with tempfile.TemporaryDirectory(prefix="yozora-stop-") as directory:
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{server.server_address[1]}/", str(Path(directory) / "stop.png"), "320", "480"],
                                capture_output=True, text=True, timeout=120,
                                env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_STOP_SCRIPTS="1500", YOZORA_RENDER_SETTLE_MS="2500"))
    server.shutdown()
    assert result.returncode == 0, result
    away = re.search(r"core thread away for (\d+) ms, stop scripts returns (\d+)", result.stderr)
    assert away, result.stderr
    # the stall is seen after the limit and the interface thread stops the script that runs
    assert int(away.group(1)) >= 1500 and away.group(2) == "1", away.group(0)
    idle = re.search(r"stop scripts with nothing running returns (\d+), stalled (\d+) ms", result.stderr)
    assert idle and idle.group(1) == "0" and int(idle.group(2)) < 1000, idle and idle.group(0)
    titles = re.findall(r"yozora: \[\s*\d+ ms\] (\S.*)", result.stderr)
    # both timers were due while the script ran, the page has to reach the last one
    assert "spinning" in titles and titles[-1] == "alive again", titles
    print("core stop: a script that never returns is stopped from the interface thread, the page and its later scripts go on")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_stop.py yozora-render")
    main()
