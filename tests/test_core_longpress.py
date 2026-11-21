import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
# a captcha checkbox is a small target: a person aims at it and holds past the half second of the long press,
# and the page still has to see the click. the button, the text and the link are three places to hold the
# finger
PAGE = """<!doctype html><meta charset=utf-8><body style='margin:0'>
<div id=box style='position:absolute;left:20px;top:20px;width:40px;height:40px;background:#468' onclick="document.title='clicks=' + (++window.n) + ' sel=' + String(getSelection())"></div>
<p id=text style='position:absolute;left:20px;top:100px;margin:0;font-size:24px'>some words to select</p>
<a id=link href='/next' style='position:absolute;left:20px;top:200px;font-size:24px'>a link to hold</a>
<img id=pic src='/pic.png' style='position:absolute;left:20px;top:260px;width:60px;height:60px'>
<script>window.n = 0; document.title = 'clicks=0 sel=';</script>"""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        if self.path == "/pic.png":
            body = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==")
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        body = (PAGE if self.path == "/" else "<!doctype html><title>next page</title>").encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def hold(binary, url, directory, actions):
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "hold.png"), "320", "372"],
                            capture_output=True, text=True, timeout=120, env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_ACTIONS=actions))
    assert result.returncode == 0, result
    title = re.search(r"yozora-render: title (.*)", result.stderr)
    assert title, result.stderr
    return title.group(1), result.stderr


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}/"
    with tempfile.TemporaryDirectory(prefix="yozora-longpress-") as directory:
        # held on a control: nothing is selected, so the release clicks it once
        title, _ = hold(binary, base, directory, "hold:40,40;wait:400")
        assert title == "clicks=1 sel=", title
        # held on text: the word is selected and nothing is clicked
        title, _ = hold(binary, base, directory, "hold:60,115;wait:400")
        assert title == "clicks=0 sel=", title
        title, log = hold(binary, base, directory, "hold:60,115;tap:40,40;wait:400")
        assert title.startswith("clicks=1 sel=") and "words" in title or "some" in title or "to" in title, title
        # held on a link: its sheet opens and the page does not navigate
        title, log = hold(binary, base, directory, "hold:30,212;wait:400")
        assert title == "clicks=0 sel=", title
        assert "long press sheet link " + base + "next" in log, log
        # held on an image: its sheet reports the image address, which the download action of the sheet takes
        title, log = hold(binary, base, directory, "hold:40,290;wait:400")
        assert "image " + base + "pic.png" in log, log
        # an ordinary tap is still one click
        title, _ = hold(binary, base, directory, "tap:40,40;wait:400")
        assert title == "clicks=1 sel=", title
    server.shutdown()
    print("core long press: a finger held on a control clicks it at release, on text it selects, on a link it opens the sheet and nothing navigates")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_longpress.py yozora-render")
    main()
