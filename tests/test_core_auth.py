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
GOOD = "Basic " + base64.b64encode(b"alice:wonder").decode()
requests = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        requests.append((self.path, self.headers.get("Authorization")))
        if self.path == "/open":
            self.reply(200, "text/html", b"<!doctype html><title>open</title><img src='/private-img'>")
        elif self.path in ("/private", "/private-img"):
            if self.headers.get("Authorization") == GOOD:
                self.reply(200, "text/html", b"<!doctype html><title>secret</title>welcome")
            else:
                self.reply(401, "text/html", b"<!doctype html><title>denied</title>no entry", {"WWW-Authenticate": 'Basic realm="Zone 51"'})
        elif self.path == "/other":
            self.reply(401, "text/html", b"<!doctype html><title>other denied</title>", {"WWW-Authenticate": "Bearer realm=x"})
        else:
            self.reply(404, "text/html", b"")

    def reply(self, status, kind, body, headers=None):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)


def run(binary, url, answers):
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, "/dev/null", "320", "480"],
                            capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_AUTH=answers, YOZORA_RENDER_SETTLE_MS="600"))
    questions = re.findall(r"auth question host (\S+) port (\d+) realm (.*?) scheme (\S+) secure (\d) failures (\d+) answer (\S+)", result.stderr)
    titles = re.findall(r"yozora-render: title (.*)", result.stderr)
    return questions, titles[-1] if titles else "", result.stderr


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    # the right name and password: one question, then the page
    questions, title, log = run(binary, base + "/private", "alice:wonder")
    assert len(questions) == 1 and questions[0][2] == "Zone 51" and questions[0][3] == "basic" and questions[0][5] == "0", (questions, log)
    assert title == "secret", (title, log)
    # a wrong password is asked about again, with the failure counted
    questions, title, log = run(binary, base + "/private", "alice:nope,alice:wonder")
    assert [q[5] for q in questions] == ["0", "1"] and title == "secret", (questions, title, log)
    # refusing shows the page of the server
    questions, title, log = run(binary, base + "/private", "cancel")
    assert len(questions) == 1 and title == "denied", (questions, title, log)
    # a wrong password and then a refusal also ends with the page of the server
    questions, title, log = run(binary, base + "/private", "alice:nope,cancel")
    assert [q[5] for q in questions] == ["0", "1"] and title == "denied", (questions, title, log)
    # a picture behind a login does not ask, and a scheme this browser does not answer does not either
    questions, title, log = run(binary, base + "/open", "alice:wonder")
    assert questions == [] and title == "open", (questions, title, log)
    questions, title, log = run(binary, base + "/other", "alice:wonder")
    assert questions == [] and title == "other denied", (questions, title, log)
    server.shutdown()
    print("core auth: a login is asked for the page, kept for the run, asked again when refused, and not asked for pictures")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_auth.py yozora-render")
    main()
