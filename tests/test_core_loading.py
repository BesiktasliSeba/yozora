from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

ROOT = Path(__file__).resolve().parent.parent
PNG_1X1 = bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360f8cfc0f01f0005000201e221bc330000000049454e44ae426082")
CHUNKS = 40
requests = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        requests.append(self.path)
        if self.path == "/ok.png":
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(PNG_1X1)))
            self.end_headers()
            self.wfile.write(PNG_1X1)
            return
        if self.path != "/slow":
            self.send_error(404)
            return
        port = self.server.server_address[1]
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        # the blocked host never resolves: the core fails these requests before any connection opens
        head = (
            "<!doctype html><html><head><meta charset=utf-8><title>slow</title>"
            "<style>body{margin:0;font:12px sans-serif}p{margin:0;height:14px}</style>"
            f"<script src='http://stats.g.doubleclick.net:{port}/analytics.js'></script></head><body>"
            f"<img src='http://www.google-analytics.com:{port}/pixel.gif' width=1 height=1>"
            "<img src='/ok.png' width=1 height=1>"
        )
        self.wfile.write(head.encode())
        self.wfile.flush()
        for number in range(CHUNKS):
            self.wfile.write(f"<p>line {number}</p>".encode())
            self.wfile.flush()
            time.sleep(0.05)
        self.wfile.write(b"<div style='position:fixed;left:0;top:0;width:100%;height:100%;background:#0000ff'></div></body></html>")


def read_png(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", data[:8]
    position, header, compressed = 8, None, b""
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed += body
        position += 12 + length
    width, height, depth, color = header[:4]
    assert depth == 8 and color == 6, header
    raw = zlib.decompress(compressed)
    stride = width * 4
    rows, previous = [], bytearray(stride)
    for row in range(height):
        start = row * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = line[i - 4] if i >= 4 else 0
            up = previous[i]
            corner = previous[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:
                estimate = left + up - corner
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - corner))
                best = (left, up, corner)[distances.index(min(distances))]
                line[i] = (line[i] + best) & 255
        rows.append(line)
        previous = line
    return width, height, rows


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    with tempfile.TemporaryDirectory(prefix="yozora-loading-") as directory:
        output = Path(directory) / "page.png"
        environment = dict(os.environ, YOZORA_LOG_REQUESTS="1")
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{port}/slow", str(output)],
                                capture_output=True, text=True, timeout=90, env=environment)
        assert result.returncode == 0, result
        assert "yozora-render: loaded and painted page" in result.stdout, result
        errors = result.stderr
        # both listed hosts are stopped, the same-origin image goes through
        assert "net: blocked  stats.g.doubleclick.net" in errors, errors
        assert "net: blocked  www.google-analytics.com" in errors, errors
        assert "yozora-render: 2 blocked requests" in errors, errors
        assert "/ok.png" in requests, requests
        # about 2 seconds of 50 ms chunks: a rendering update per change would be about 40, the throttle allows 4 per second
        statistics = re.search(r"events \d+ ms, (\d+) rendering updates \d+ ms, (\d+) layouts", errors)
        updates, layouts = int(statistics.group(1)), int(statistics.group(2))
        assert 2 <= updates <= 14, errors
        # a layout per chunk would be 40 or more, the loading delay keeps it near the four per second
        assert 2 <= layouts <= 14, errors
        # the last change arrived inside a throttle window and still reaches the picture
        width, height, rows = read_png(output)
        assert (width, height) == (320, 400), (width, height)
        assert bytes(rows[200][160 * 4:160 * 4 + 4]) == bytes([0, 0, 255, 255]), bytes(rows[200][160 * 4:160 * 4 + 4])

    server.shutdown()
    print(f"core loading: tracker hosts blocked, {updates} rendering updates, {layouts} layouts, final change painted")


if __name__ == "__main__":
    main()
