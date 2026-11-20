from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent

# every hop waits for the one before: a thread that only looks at its run loop every 33 ms makes each of them cost a whole interval
PAGE = b"""<!doctype html><title>runloop</title><body><script>
var out = {};
function chainTimeout(n, done) { var t = performance.now(), i = 0; (function f() { if (++i < n) setTimeout(f, 0); else { out.timeout = Math.round(performance.now() - t); done(); } })(); }
function chainMessage(n, done) { var t = performance.now(), i = 0, c = new MessageChannel(); c.port1.onmessage = function () { if (++i < n) c.port2.postMessage(0); else { out.message = Math.round(performance.now() - t); done(); } }; c.port2.postMessage(0); }
function chainFetch(n, done) { var t = performance.now(), i = 0; (function f() { fetch('/ping').then(function (r) { return r.text(); }).then(function () { if (++i < n) f(); else { out.fetch = Math.round(performance.now() - t); done(); } }); })(); }
chainMessage(100, function () { chainTimeout(50, function () { chainFetch(10, function () { console.log('hops ' + JSON.stringify(out)); }); }); });
</script>"""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        body = PAGE if self.path == "/" else b"pong"
        self.send_response(200)
        self.send_header("Content-Type", "text/html" if self.path == "/" else "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)


def main(binary):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory() as directory:
        environment = dict(os.environ, YOZORA_RENDER_SETTLE_MS="4000")
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{server.server_address[1]}/", str(Path(directory) / "page.png")],
                                capture_output=True, text=True, timeout=90, env=environment)
    server.shutdown()
    assert result.returncode == 0, result
    match = re.search(r'hops \{"message":(\d+),"timeout":(\d+),"fetch":(\d+)\}', result.stderr)
    assert match, result.stderr
    message, timeout, fetch = (int(value) for value in match.groups())
    # a 33 ms interval would put these at 3300 ms, 1650 ms (at least) and 330 ms
    assert message < 1000, message
    assert timeout < 1000, timeout
    assert fetch < 300, fetch
    print(f"core run loop: 100 message hops {message} ms, 50 timer hops {timeout} ms, 10 fetches {fetch} ms")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_runloop.py yozora-render")
    main(sys.argv[1])
