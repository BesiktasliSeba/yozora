from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
PNG_1X1 = bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360f8cfc0f01f0005000201e221bc330000000049454e44ae426082")
IMAGES = 30
order = []
lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        with lock:
            order.append(self.path)
        if self.path == "/":
            body = ("<!doctype html><meta charset=utf-8><body>" + "".join(f"<img src=/slow/{n}.png width=1 height=1>" for n in range(IMAGES))
                    + "<link rel=stylesheet href=/late.css><p>text</p></body>").encode()
            kind = "text/html"
        elif self.path == "/late.css":
            body, kind = b"p{color:red}", "text/css"
        else:
            # every picture takes long enough for the connection slots to fill up
            time.sleep(0.4)
            body, kind = PNG_1X1, "image/png"
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="yozora-priority-") as directory:
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{server.server_address[1]}/", str(Path(directory) / "page.png")],
                                capture_output=True, text=True, timeout=120)
    server.shutdown()
    assert result.returncode == 0, result
    position = order.index("/late.css")
    # the page and the first twelve pictures take the slots, in first come order the stylesheet would be request 32
    assert position <= 14, (position, order[:20])
    assert len([path for path in order if path.startswith("/slow/")]) == IMAGES, order
    print(f"core priority: stylesheet requested as number {position + 1} of {len(order)}, behind the first slots only")


if __name__ == "__main__":
    main()
