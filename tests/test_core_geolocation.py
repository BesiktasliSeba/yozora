from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
PAGE = """<!doctype html><meta charset=utf-8><body><button id=b style='width:200px;height:100px'>where</button>
<script>
var R = {api: typeof navigator.geolocation, fixes: []};
// the probe of the test runs in an isolated world, so the results go to an attribute of the page
setInterval(function () { document.documentElement.setAttribute('data-r', JSON.stringify(R)); }, 50);
document.getElementById('b').onclick = function () {
    navigator.geolocation.getCurrentPosition(function (p) {
        var c = p.coords;
        R.once = {lat: c.latitude, lon: c.longitude, accuracy: c.accuracy, altitude: c.altitude, speed: c.speed};
    }, function (e) { R.once = {error: e.code, message: e.message}; });
    if (location.search == '?watch') {
        navigator.geolocation.watchPosition(function (p) { R.fixes.push([p.coords.latitude, p.coords.longitude]); }, function (e) { R.watchError = e.code; });
    }
};
</script>"""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        body = PAGE.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def run(binary, url, directory, actions):
    probe = Path(directory) / "probe.js"
    probe.write_text("(function(){return document.documentElement.getAttribute('data-r')})()")
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "geo.png"), "320", "480"],
                            capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, YOZORA_RENDER_ACTIONS=actions, YOZORA_RENDER_SCRIPT=str(probe)))
    assert result.returncode == 0, result
    found = re.search(r"script result (.*)", result.stderr)
    assert found, result.stderr
    return json.loads(found.group(1)), result.stderr


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}/"
    with tempfile.TemporaryDirectory(prefix="yozora-geo-") as directory:
        # allowed: the question names the origin, the page gets the position the interface passes on and a
        # position outside the globe is dropped
        r, log = run(binary, base, directory, "tap:100,50;wait:300;geo-ask:1;wait:300;geo-pos:95,10,5;geo-pos:55.75,37.62,12;wait:500")
        assert r["api"] == "object", r
        assert re.search(r"geolocation question \d+ from http://127\.0\.0\.1:%d" % server.server_address[1], log), log
        assert "geolocation wanted 1" in log, log
        assert r["once"] == {"lat": 55.75, "lon": 37.62, "accuracy": 12, "altitude": None, "speed": None}, r
        # a watch gets every fix
        r, log = run(binary, base + "?watch", directory, "tap:100,50;wait:300;geo-ask:1;wait:300;geo-pos:10,20,5;wait:200;geo-pos:10.5,20.5,5;wait:500")
        assert r["fixes"][-2:] == [[10, 20], [10.5, 20.5]] or r["fixes"] == [[10.5, 20.5]], r
        # refused by the person: the page hears PERMISSION_DENIED (1)
        r, log = run(binary, base, directory, "tap:100,50;wait:300;geo-ask:0;wait:500")
        assert r["once"].get("error") == 1, r
        # the system has no position: the page hears POSITION_UNAVAILABLE (2)
        r, log = run(binary, base, directory, "tap:100,50;wait:300;geo-ask:1;wait:300;geo-error:0;wait:500")
        assert r["once"].get("error") == 2, r
    server.shutdown()
    print("core geolocation: the question names the origin, positions reach getCurrentPosition and watchPosition, refusal and failure reach the page, a position off the globe is dropped")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_geolocation.py yozora-render")
    main()
