from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_core_loading import ROOT, read_png

# red until a script paints it green
PAGE = b"""<!doctype html><meta charset=utf-8><style>
body{margin:0;background:#ff0000}
@font-face{font-family:Sent;src:url(/font.ttf)}
#sample{font:30px Sent,serif}
@media (prefers-reduced-motion: reduce){#motion{position:absolute;left:0;top:300px;width:50px;height:50px;background:#0000ff}}
#box{position:absolute;left:0;top:200px;font:10px sans-serif;background:#ffff00;width:20px}
</style><body><div id=box>x</div><div id=motion></div><div id=sample>sample</div><script>document.body.style.background = '#00ff00'</script>"""

# a page of the first server loads an image of the second twice: the second server answers with a cookie the
# first time and records what it receives the second
cross = []


class Other(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        cross.append(self.headers.get("Cookie") or "")
        self.send_response(200)
        self.send_header("Set-Cookie", "tracker=1; Max-Age=3600; Path=/")
        self.send_header("Content-Type", "image/gif")
        self.send_header("Content-Length", "0")
        self.end_headers()


fonts_requested = []


class Main(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    other_port = 0

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        if self.path == "/font.ttf":
            fonts_requested.append(self.path)
            font = (ROOT / "app/fonts/Roboto-Bold.ttf").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "font/ttf")
            self.send_header("Content-Length", str(len(font)))
            self.end_headers()
            self.wfile.write(font)
            return
        body = PAGE if self.path == "/" else (
            f"<!doctype html><body><img src='http://localhost:{Main.other_port}/a.gif'><script>setTimeout(function(){{var i=new Image();i.src='http://localhost:{Main.other_port}/b.gif'}},500)</script>".encode())
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def render(binary, url, options, extra=None):
    with tempfile.TemporaryDirectory(prefix="yozora-options-") as directory:
        output = Path(directory) / "page.png"
        env = dict(os.environ, YOZORA_RENDER_OPTIONS=options, YOZORA_RENDER_WAIT_MS="1500", YOZORA_RENDER_SITE_DATA=directory)
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(output)], capture_output=True, text=True, timeout=90, env=env)
        assert result.returncode == 0, result
        return read_png(output)


def pixel(image, x, y):
    width, height, rows = image
    return bytes(rows[y][x * 4:x * 4 + 4])


def yellow_rows(image):
    width, height, rows = image
    return sum(1 for row in rows if bytes(row[2 * 4:2 * 4 + 4]) == bytes([255, 255, 0, 255]))


def main():
    binary = str(Path(sys.argv[1]).resolve())
    main_server = ThreadingHTTPServer(("127.0.0.1", 0), Main)
    other_server = ThreadingHTTPServer(("127.0.0.1", 0), Other)
    Main.other_port = other_server.server_address[1]
    for server in (main_server, other_server):
        threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{main_server.server_address[1]}/"
    # pixels are RGBA
    scripts_on = render(binary, url, "javascript=1")
    scripts_off = render(binary, url, "javascript=0")
    zoomed = render(binary, url, "zoom=200")
    normal = render(binary, url, "zoom=100")
    assert pixel(scripts_on, 300, 5) == bytes([0, 255, 0, 255]), pixel(scripts_on, 300, 5)
    assert pixel(scripts_off, 300, 5) == bytes([255, 0, 0, 255]), pixel(scripts_off, 300, 5)
    # the box holds one line of 10 px text, twice as tall at 200 percent
    assert yellow_rows(zoomed) >= yellow_rows(normal) * 1.8, (yellow_rows(zoomed), yellow_rows(normal))
    reduced = render(binary, url, "motion=0")
    # a page that asks for less motion gets it: the box only exists under prefers-reduced-motion
    assert pixel(reduced, 20, 320) == bytes([0, 0, 255, 255]), pixel(reduced, 20, 320)
    assert pixel(scripts_on, 20, 320) != bytes([0, 0, 255, 255])
    fonts_requested.clear()
    render(binary, url, "fonts=1")
    with_fonts = len(fonts_requested)
    fonts_requested.clear()
    render(binary, url, "fonts=0")
    without_fonts = len(fonts_requested)
    assert with_fonts >= 1 and without_fonts == 0, (with_fonts, without_fonts)
    cross.clear()
    render(binary, f"http://127.0.0.1:{main_server.server_address[1]}/cross", "third=0")
    accepted = list(cross)
    cross.clear()
    render(binary, f"http://127.0.0.1:{main_server.server_address[1]}/cross", "third=1")
    blocked = list(cross)
    main_server.shutdown()
    other_server.shutdown()
    # a site other than the page may set a cookie unless the user refuses them, and then it never sees it again
    assert len(accepted) == 2 and "tracker=1" in accepted[1], accepted
    assert len(blocked) == 2 and blocked[1] == "", blocked
    print("core options: scripts off, text zoom, reduced motion, no web fonts and third party cookies take effect")


if __name__ == "__main__":
    main()
