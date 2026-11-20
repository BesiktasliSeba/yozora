from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
requests = []

# fields sit at fixed places: button at y 10, text field at y 60, textarea at y 110, link at y 200, target link at y 250
PAGE = b"""<!doctype html><html><head><meta charset=utf-8><title>start</title>
<style>body{margin:0}input,button,textarea,a{position:absolute;left:10px;width:200px;height:30px;box-sizing:border-box;margin:0;font:14px sans-serif;display:block}</style></head><body>
<button style="top:10px" onclick="document.title='clicked'">go</button>
<form action="/result" method="get"><input name=q style="top:60px" oninput="document.title='v:'+this.value"></form>
<textarea style="top:110px;height:60px" oninput="document.title='t:'+this.value.replace(/\\n/g,'|')"></textarea>
<a href="/result?link=1" style="top:200px">link</a>
<a href="/result?target=1" target="_blank" style="top:250px">target</a>
<script>window.open('/result?popup=1')</script></body></html>"""
RESULT = b"<!doctype html><title>result</title><body>done"


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        requests.append(self.path)
        body = PAGE if self.path == "/" else RESULT
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def render(binary, port, actions):
    with tempfile.TemporaryDirectory(prefix="yozora-input-") as directory:
        environment = dict(os.environ, YOZORA_RENDER_ACTIONS=actions)
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{port}/", str(Path(directory) / "page.png")],
                                capture_output=True, text=True, timeout=90, env=environment)
    assert result.returncode == 0, result
    kinds = [int(line.split()[3]) for line in result.stderr.splitlines() if line.startswith("yozora-render: input kind")]
    title = [line[len("yozora-render: title "):] for line in result.stderr.splitlines() if line.startswith("yozora-render: title ")][-1]
    return kinds, title, result.stderr


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]

    # a tap is a full click for the page scripts
    kinds, title, _ = render(binary, port, "tap:50,25")
    assert (kinds, title) == ([0], "clicked"), (kinds, title)
    # a text field asks for the keyboard and gets typed text and backspace
    kinds, title, _ = render(binary, port, "tap:50,75;type:hi there;delete;delete")
    assert kinds == [1, 1, 1, 1] and title == "v:hi the", (kinds, title)
    # return in a text field submits its form with the typed value
    kinds, title, _ = render(binary, port, "tap:50,75;type:abc d\\n")
    assert title == "result" and "/result?q=abc+d" in requests, (title, requests)
    # a textarea keeps the line break
    kinds, title, _ = render(binary, port, "tap:50,135;type:x\\ny")
    assert kinds == [2, 2] and title == "t:x|y", (kinds, title)
    # a tap outside any field asks for nothing
    kinds, title, errors = render(binary, port, "tap:50,75;tap:150,350;type:z")
    assert kinds == [1, 0, 0] and "nothing accepts text" in errors, (kinds, errors)
    # links open, a target link opens in the same view, a window opened by a script at load time does not
    kinds, title, _ = render(binary, port, "tap:50,215")
    assert title == "result" and "/result?link=1" in requests, (title, requests)
    kinds, title, _ = render(binary, port, "tap:50,265")
    assert title == "result" and "/result?target=1" in requests, (title, requests)
    assert "/result?popup=1" not in requests, requests

    server.shutdown()
    print("core input: click, text, backspace, submit, textarea, links and popups passed")


if __name__ == "__main__":
    main()
