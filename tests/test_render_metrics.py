from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *arguments):
        pass

    def do_GET(self):
        if self.path == "/animation":
            page = """<!doctype html><title>animation</title><body style='background:red'>
<script>requestAnimationFrame(() => {
    const start = performance.now(); while (performance.now() - start < 25) {}
    document.body.style.background = 'blue';
});</script>"""
        elif self.path == "/final":
            page = "<!doctype html><title>final</title><body style='background:blue'>done"
        else:
            target = "/final" if self.path == "/navigate" else "http://127.0.0.1:1/refused"
            page = f"""<!doctype html><title>first</title><body style='background:red'>first
<script>onload = () => setTimeout(() => location.href = '{target}', 200);</script>"""
        body = page.encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix="yozora-metrics-") as directory:
            for route, status in (("navigate", 0), ("fail", 1)):
                output = Path(directory) / (route + ".png")
                result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                                         f"http://127.0.0.1:{server.server_port}/{route}", str(output)],
                                        capture_output=True, text=True, timeout=60,
                                        env=dict(os.environ, YOZORA_RENDER_SETTLE_MS="600"))
                assert result.returncode == status, result
                assert output.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"), result.stderr
                if status:
                    assert "saved partial page after incomplete load" in result.stdout, result
                    continue
                assert "yozora-render: title final\n" in result.stderr, result.stderr
                timing = re.search(r"timing startup_ms=(\d+) load_ms=(\d+) first_tile_ms=(\d+) initial_state=2", result.stderr)
                resources = re.search(r"resources resident_kb=(\d+) peak_resident_kb=(\d+) user_ms=(\d+) system_ms=(\d+)", result.stderr)
                total = re.search(r"(\d+) ms total after request", result.stderr)
                assert timing and resources and total, result.stderr
                assert int(total[1]) >= int(timing[2]) + 550, result.stderr
                assert int(timing[3]) <= int(total[1]), result.stderr
                assert int(resources[1]) > 0 and int(resources[2]) > 0, result.stderr
                assert "yozora-trace:" not in result.stderr, result.stderr
            output = Path(directory) / "animation.png"
            result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                                     f"http://127.0.0.1:{server.server_port}/animation", str(output)],
                                    capture_output=True, text=True, timeout=60,
                                    env=dict(os.environ, YOZORA_RENDER_SETTLE_MS="1300", YOZORA_TRACE_RENDERING="1",
                                             YOZORA_RENDER_SNAPSHOT_MS="1000", YOZORA_RENDER_TIMEOUT="5"))
            assert result.returncode == 0, result
            phases = re.findall(r"phase=(\S+) begin_us=(\d+) end_us=(\d+)", result.stderr)
            assert any(name == "AnimationFrameCallbacks" and int(end) - int(begin) >= 24000
                       for name, begin, end in phases), result.stderr
            assert any(name == "style" for name, _, _ in phases), result.stderr
            assert any(name == "layout" for name, _, _ in phases), result.stderr
            snapshots = list(Path(directory).glob("animation.png.at-*.png"))
            assert snapshots and len(snapshots) <= 2, result.stderr
            assert all(path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n") for path in snapshots)
            for name, value in (("YOZORA_RENDER_TIMEOUT", "-1"), ("YOZORA_RENDER_TIMEOUT", "garbage"),
                                ("YOZORA_RENDER_SETTLE_MS", "180001"), ("YOZORA_RENDER_SNAPSHOT_MS", "1")):
                invalid = subprocess.run([binary, "unused", "unused", "unused", "unused"],
                                         capture_output=True, text=True, timeout=5, env=dict(os.environ, **{name: value}))
                assert invalid.returncode == 2 and name in invalid.stderr, invalid
    finally:
        server.shutdown()
        server.server_close()
    print("render metrics: navigation, failure pixels, resource values, callback timing, snapshots and bounded probe settings passed")


if __name__ == "__main__":
    main()
