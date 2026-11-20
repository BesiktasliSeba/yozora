from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
# four lines of 40 points, the second has two words
PAGE = ("<!doctype html><meta charset=utf-8><body style='margin:0;font:24px/40px Roboto'>"
        "<div>alpha</div><div>beta gamma</div><div>delta</div><div>epsilon</div>"
        "<div style='user-select:none;-webkit-user-select:none'>locked</div>").encode()


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


def select(binary, url, directory, steps):
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "select.png"), "320", "480"],
                            capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_SELECT=steps, YOZORA_RENDER_SETTLE_MS="500"))
    assert result.returncode == 0, result
    found = re.findall(r"selection (\S+) active (\d) start (-?\d+),(-?\d+),(-?\d+) end (-?\d+),(-?\d+),(-?\d+) text (.*)", result.stderr)
    assert found, result.stderr
    return found


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    with tempfile.TemporaryDirectory(prefix="yozora-select-") as directory:
        steps = select(binary, url, directory, "word:30,20;word:20,60;word:100,60;range:20,20,60,140;all;clear;word:30,180")
    server.shutdown()
    word, second, third, range_, everything, cleared, locked = steps
    # a long press selects the word under the finger and reports where its two ends are, the second word of a line is its own
    assert word[1] == "1" and word[8] == "alpha" and int(word[3]) == 4 and int(word[5]) > int(word[2]), word
    assert second[8] == "beta" and int(second[3]) == 44, second
    assert third[8] == "gamma", third
    # dragging an end takes whole words of every line in between
    assert range_[8] == "alpha|beta gamma|delta|epsilon", range_
    assert everything[8].startswith("alpha|beta gamma|delta|epsilon"), everything
    # nothing stays selected after clear, and text that the page does not let the user select is not selected
    assert cleared[1] == "0" and cleared[8] == "", cleared
    assert locked[1] == "0", locked
    print("core select: the word under a point, a range between two points and all of the page are selected and cleared")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_select.py yozora-render")
    main()
