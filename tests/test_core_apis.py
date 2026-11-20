from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_core_loading import ROOT, read_png

# the first touch of window.indexedDB aborted the core while the page had no database provider, and
# Notification.permission dereferenced a missing notification client on the device
PAGE = b"""<!doctype html><html><head><meta charset=utf-8><title>apis</title></head>
<body style='margin:0;background:#ff0000'><script>
var available = typeof indexedDB === 'object' && typeof IDBKeyRange === 'function' && ('indexedDB' in window) && !!indexedDB.open('apis');
requestAnimationFrame(function () {
    // sites feature-test Notification inside animation callbacks and event handlers
    var notifications = typeof Notification === 'undefined' && !('Notification' in window);
    document.body.style.background = available && notifications ? '#0000ff' : '#ff0000';
});
</script></body></html>"""


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
    with tempfile.TemporaryDirectory(prefix="yozora-apis-") as directory:
        output = Path(directory) / "page.png"
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{server.server_address[1]}/", str(output)],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result
        width, height, rows = read_png(output)
        pixel = bytes(rows[height // 2][width // 2 * 4:width // 2 * 4 + 4])
        assert pixel == bytes([0, 0, 255, 255]), pixel
    server.shutdown()
    print("core apis: indexedDB opens, Notification is absent and reading them does not abort")


if __name__ == "__main__":
    main()
