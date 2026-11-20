from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent
BODY = bytes(range(256)) * 1200


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        path = self.path
        headers = {}
        status = 200
        body = BODY
        kind = "application/octet-stream"
        if path == "/file.bin":
            pass
        elif path == "/attach":
            kind = "text/plain; charset=utf-8"
            headers["Content-Disposition"] = "attachment; filename=\"../résumé (1).txt\"".encode("utf-8").decode("latin-1")
            body = b"attached text"
        elif path == "/redirect":
            status = 302
            headers["Location"] = "/file.bin"
            body = b""
        elif path == "/cookie":
            status = 302
            headers["Set-Cookie"] = "ticket=42; Path=/"
            headers["Location"] = "/secret"
            body = b""
        elif path == "/secret":
            if "ticket=42" in self.headers.get("Cookie", ""):
                body = b"secret file"
            else:
                status = 403
                body = b"no cookie"
        elif path == "/missing":
            status = 404
            kind = "text/html"
            body = b"gone"
        elif path == "/page":
            kind = "text/html"
            body = b"<!doctype html><title>page</title>text"
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        for name, value in headers.items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)


def run(binary, url, directory):
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(Path(directory) / "page.png"), "320", "480"],
                            capture_output=True, text=True, timeout=120,
                            env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_DOWNLOADS=str(Path(directory) / "Downloads")))
    assert result.returncode in (0, 1), result
    return re.findall(r"download state (\d) received (\d+) total (\d+) name (.*?) error (.*)", result.stderr), result.stderr


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    with tempfile.TemporaryDirectory(prefix="yozora-download-") as directory:
        folder = Path(directory) / "Downloads"
        # a type that no viewer shows is saved, with the name the address gives it
        listed, log = run(binary, base + "/file.bin", directory)
        assert len(listed) == 1 and listed[0][0] == "2" and listed[0][3] == "file.bin", (listed, log)
        assert (folder / "file.bin").read_bytes() == BODY and int(listed[0][1]) == len(BODY) == int(listed[0][2])
        assert not list(folder.glob("*.part"))
        # the same name again does not replace the file
        listed, _ = run(binary, base + "/file.bin", directory)
        assert listed[0][3] == "file (1).bin" and (folder / "file (1).bin").read_bytes() == BODY, listed
        # an attachment of a type the page could show, named by the server: the path part of the name is gone
        listed, _ = run(binary, base + "/attach", directory)
        assert listed[0][0] == "2" and listed[0][3] == "_r\u00e9sum\u00e9 (1).txt", listed
        assert (folder / listed[0][3]).read_bytes() == b"attached text", listed
        # a redirect is followed, a cookie that the page got on the way is sent with the file
        listed, _ = run(binary, base + "/redirect", directory)
        assert listed[0][0] == "2" and listed[0][3].startswith("file"), listed
        listed, log = run(binary, base + "/cookie", directory)
        assert listed[0][0] == "2" and listed[0][3].startswith("secret"), (listed, log)
        # a page is not a download
        listed, _ = run(binary, base + "/page", directory)
        assert listed == [], listed
    server.shutdown()
    print("core download: unsupported types and attachments are saved by name, redirects and cookies work, a name is never taken twice")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_download.py yozora-render")
    main()
