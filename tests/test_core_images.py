import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import struct
import threading
import time
import zlib

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("loading", ROOT / "tests/test_core_loading.py")
loading = importlib.util.module_from_spec(spec)
spec.loader.exec_module(loading)

PAGE = "<!doctype html><meta charset=utf-8><body style='margin:0'>{}</body>"


def render(binary, directory, port, name):
    output = Path(directory) / (name + ".png")
    url = f"http://127.0.0.1:{port}/{name}.html"
    child = subprocess.Popen([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(output)],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # wait4 reports the peak resident size of this child alone
    _, status, usage = os.wait4(child.pid, 0)
    assert os.waitstatus_to_exitcode(status) == 0, name
    return output, usage.ru_maxrss // 1024


class SlowPhotoHandler(SimpleHTTPRequestHandler):
    def log_message(self, *arguments):
        pass

    def do_GET(self):
        if self.path != "/slow.jpg":
            super().do_GET()
            return
        data = (ROOT / "tests/data/gradient-3200x2400.jpg").read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "image/jpeg")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        # a slow link: the decoder sees the file grow while the page already draws it
        step = len(data) // 12 + 1
        for start in range(0, len(data), step):
            self.wfile.write(data[start:start + step])
            self.wfile.flush()
            time.sleep(0.12)


def solid_png(width, height):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    raw = (b"\0" + bytes(width * 3)) * height
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 1)) + chunk(b"IEND", b"")


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="yozora-images-") as directory:
        site = Path(directory) / "site"
        site.mkdir()
        (site / "photo.jpg").write_bytes((ROOT / "tests/data/gradient-3200x2400.jpg").read_bytes())
        (site / "empty.html").write_text(PAGE.format("<p>no image</p>"))
        # the photo is drawn at 320 by 240 points, one hundredth of its pixels
        (site / "photo.html").write_text(PAGE.format("<img src=photo.jpg style='display:block;width:320px;height:240px'>"))
        (site / "slow.html").write_text(PAGE.format("<img src=slow.jpg style='display:block;width:320px;height:240px'>"))
        # 6000 by 6000 is 36 megapixels, a full size frame of it would be 144 MB
        (site / "huge.png").write_bytes(solid_png(6000, 6000))
        (site / "huge.html").write_text(PAGE.format("<img src=huge.png style='display:block;width:320px;height:240px'>"))
        handler = functools.partial(SlowPhotoHandler, directory=str(site))
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        port = server.server_address[1]
        _, baseline = render(binary, directory, port, "empty")
        output, peak = render(binary, directory, port, "photo")
        slow_output, slow_peak = render(binary, directory, port, "slow")
        _, huge_peak = render(binary, directory, port, "huge")
        server.shutdown()
        width, height, rows = loading.read_png(output)
        assert (width, height) == (320, 400), (width, height)
        # the gradient is red to the right, green downwards and blue to the left
        for x, y in ((8, 8), (160, 120), (312, 230)):
            expected = (x * 255 // 319, y * 255 // 239, 255 - x * 255 // 319)
            actual = tuple(rows[y][x * 4:x * 4 + 3])
            assert all(abs(a - e) <= 8 for a, e in zip(actual, expected)), (x, y, actual, expected)
        # below the photo the page is white
        assert tuple(rows[300][0:4]) == (255, 255, 255, 255), tuple(rows[300][0:4])
        # a full size frame is 3200 * 2400 * 4 = 30 MB, the drawn size needs well under a tenth of that
        growth = peak - baseline
        assert growth < 14, (baseline, peak)
        # the same file arriving in pieces must not fall back to a full size frame while it is incomplete
        slow_growth = slow_peak - baseline
        assert slow_growth < 14, (baseline, slow_peak)
        _, _, slow_rows = loading.read_png(slow_output)
        for x, y in ((8, 8), (160, 120), (312, 230)):
            expected = (x * 255 // 319, y * 255 // 239, 255 - x * 255 // 319)
            actual = tuple(slow_rows[y][x * 4:x * 4 + 3])
            assert all(abs(a - e) <= 8 for a, e in zip(actual, expected)), ("slow", x, y, actual, expected)
        # the frame is refused instead of allocated
        huge_growth = huge_peak - baseline
        assert huge_growth < 30, (baseline, huge_peak)
    print(f"core images: 3200x2400 photo painted correctly, peak memory grew {growth} MB, {slow_growth} MB while it arrived slowly, a 36 megapixel png grew it {huge_growth} MB")


if __name__ == "__main__":
    main()
