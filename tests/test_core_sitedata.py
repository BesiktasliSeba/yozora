from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
# the title reports what the page found: a cookie the server set, a cookie it set itself, local storage from
# an earlier run and session storage from this one. an HttpOnly cookie must stay out of script reach
PAGE = b"""<!doctype html><meta charset=utf-8><title>start</title><body><script>
var server = document.cookie.indexOf('srv=1') >= 0;
var script = document.cookie.indexOf('own=2') >= 0;
var hidden = document.cookie.indexOf('sid=') < 0;
var local = localStorage.getItem('key') === 'value';
document.cookie = 'own=2; max-age=3600; path=/';
localStorage.setItem('key', 'value');
sessionStorage.setItem('tab', 'x');
var session = sessionStorage.getItem('tab') === 'x' && sessionStorage.length === 1;
document.title = 'found server=' + server + ' script=' + script + ' local=' + local + ' session=' + session + ' hidden=' + hidden + ' enabled=' + navigator.cookieEnabled;
</script>"""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    seen = []

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        Handler.seen.append(self.headers.get("Cookie") or "")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Set-Cookie", "srv=1; Max-Age=3600; Path=/")
        self.send_header("Set-Cookie", "sid=3; Max-Age=3600; Path=/; HttpOnly")
        self.send_header("Content-Length", str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)


def load(binary, url, data):
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(data) / "page.png")],
                            capture_output=True, text=True, timeout=60, env=dict(os.environ, YOZORA_RENDER_SITE_DATA=str(data)))
    assert result.returncode == 0, result
    return re.search(r"found server=\S+ script=\S+ local=\S+ session=\S+ hidden=\S+ enabled=\S+", result.stderr).group(0)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    with tempfile.TemporaryDirectory(prefix="yozora-sitedata-") as data:
        first = load(binary, url, data)
        second = load(binary, url, data)
        Handler.seen.clear()
        load(binary, url, data)
        sent = Handler.seen[0]
    server.shutdown()
    # the first run sees the cookie its own response set and nothing else, the next process finds cookies and
    # local storage on disk, and a page's session storage is its own
    assert first == "found server=true script=false local=false session=true hidden=true enabled=true", first
    assert second == "found server=true script=true local=true session=true hidden=true enabled=true", second
    # the server sees the cookies of the earlier runs, the HttpOnly one that the page cannot read and the script's own
    assert "srv=1" in sent and "sid=3" in sent and "own=2" in sent, sent
    print("core site data: cookies and local storage kept between runs, session storage per page, cookies sent to the server")


if __name__ == "__main__":
    main()
