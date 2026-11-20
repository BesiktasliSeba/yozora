from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
BANDS = "".join(f"<div style='height:23px;background:rgb({(n * 37) % 256},{(n * 91) % 256},{(n * 53) % 256})'></div>" for n in range(60))


def page(box, timer):
    # a small box inside the second tile changes colour after the page finished loading
    script = "setTimeout(function(){document.getElementById('box').style.background='#00ff00'},600)" if timer else ""
    return ("<!doctype html><meta charset=utf-8><body style='margin:0'>" + BANDS
            + "<div style='position:absolute;left:100px;top:100px;font:20px Roboto'>sharp text</div>"
            + f"<div id=box style='position:absolute;left:40px;top:300px;width:30px;height:30px;background:{box}'></div><script>{script}</script>").encode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        body = page("#ff0000", True) if self.path == "/change" else page("#00ff00", False)
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def render(binary, url, directory, name, **extra):
    output = Path(directory) / name
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(output), "320", "400"],
                            capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, YOZORA_RENDER_OVERSCAN="1", YOZORA_RENDER_SCALE="2", YOZORA_RENDER_WAIT_MS="1200", **extra))
    assert result.returncode == 0, result
    scales = re.search(r"tile scales (\S+) to (\S+), (\d+) painted whole, (\d+) in part", result.stderr)
    return output.read_bytes(), float(scales.group(1)), float(scales.group(2)), int(scales.group(3)), int(scales.group(4))


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    with tempfile.TemporaryDirectory(prefix="yozora-tiles-") as directory:
        changed, low, high, whole, partial = render(binary, base + "/change", directory, "changed.png")
        reference, _, _, _, _ = render(binary, base + "/still", directory, "reference.png")
        economy, economy_low, economy_high, _, _ = render(binary, base + "/still", directory, "economy.png", YOZORA_RENDER_ECONOMY="1")
        small, _, _, _, _ = render(binary, base + "/still", directory, "small.png", YOZORA_DEVICE_MEMORY_MB="256", YOZORA_DEVICE_CORES="1")
        constrained, _, _, _, _ = render(binary, base + "/still", directory, "512.png", YOZORA_DEVICE_MEMORY_MB="512")
        medium, _, _, _, _ = render(binary, base + "/still", directory, "513.png", YOZORA_DEVICE_MEMORY_MB="513")
    server.shutdown()
    # a finished page has tiles at the screen scale, 2 here, even though it was painted at 1 while it loaded
    assert (low, high) == (2.0, 2.0), (low, high)
    # the box changed after the load: its tile was painted again in part and the result is the same picture as
    # a page that had the color from the start
    assert partial >= 1, partial
    assert changed == reference, "a tile painted again in part differs from a tile painted whole"
    # economy mode keeps one pixel for each point
    assert (economy_low, economy_high) == (1.0, 1.0), (economy_low, economy_high)
    # and the stretched picture is not the sharp one: the text has edges that one pixel for each point cannot hold
    assert economy != reference
    # a phone with 256 MB keeps fewer tiles around the view: the picture of all tiles is shorter
    height = lambda png: int.from_bytes(png[20:24], "big")
    assert height(small) < height(reference), (height(small), height(reference))
    assert height(constrained) == height(reference), (height(constrained), height(reference))
    assert height(medium) == height(reference), (height(medium), height(reference))
    print(f"core tiles: partial repaint equals a whole one ({partial} in part, {whole} whole), loading page ends at scale 2, economy stays at 1, 256 MB keeps fewer tiles, 512 MB and 513 MB retain the scrolling margin")


if __name__ == "__main__":
    main()
