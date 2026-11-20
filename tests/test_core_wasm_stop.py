"""a WebAssembly function that never returns is stopped the way a script is: the interface thread asks the VM to terminate, the interrupt hook of the engine sees it
inside the loop (a loop with no calls never gives control back otherwise), and the page goes on with its later timers"""
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wasm_builder import BR, END, LOOP, Module

ROOT = Path(__file__).resolve().parent.parent


def page():
    module = Module()
    module.func([], [], LOOP() + BR(0) + END, export="spin")
    encoded = base64.b64encode(module.build()).decode()
    return ("<!doctype html><meta charset=utf-8><title>start</title><body>text<script>"
            "var raw=atob('" + encoded + "'),bytes=new Uint8Array(raw.length);for(var i=0;i<raw.length;++i)bytes[i]=raw.charCodeAt(i);"
            "var spin=new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports.spin;"
            "setTimeout(function(){document.title='spinning';spin()},100);"
            "setTimeout(function(){document.title='alive'},400);"
            "setTimeout(function(){document.title='alive again'},1200);</script>").encode()


PAGE = page()


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


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="yozora-wasm-stop-") as directory:
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{server.server_address[1]}/",
                                 str(Path(directory) / "stop.png"), "320", "480"], capture_output=True, text=True, timeout=120,
                                env=dict(os.environ, YOZORA_RENDER_SCALE="2", YOZORA_RENDER_STOP_SCRIPTS="1500", YOZORA_RENDER_SETTLE_MS="2500"))
    server.shutdown()
    assert result.returncode == 0, result
    away = re.search(r"core thread away for (\d+) ms, stop scripts returns (\d+)", result.stderr)
    assert away, result.stderr
    assert int(away.group(1)) >= 1500 and away.group(2) == "1", away.group(0)
    titles = re.findall(r"yozora: \[\s*\d+ ms\] (\S.*)", result.stderr)
    assert "spinning" in titles and titles[-1] == "alive again", titles
    print("core wasm stop: a WebAssembly loop that never returns is stopped from the interface thread, the page and its later timers go on")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_wasm_stop.py yozora-render")
    main()
