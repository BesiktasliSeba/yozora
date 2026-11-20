import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent


def main():
    binary = str(Path(sys.argv[1]).resolve())
    handler = functools.partial(type("Quiet", (SimpleHTTPRequestHandler,), {"log_message": lambda *arguments: None}), directory=str(ROOT / "tests/js"))
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory(prefix="yozora-gc-") as directory:
        # a 512 MB device is simulated by the same size limit the phone reports
        environment = dict(os.environ, JSC_logGC="true", JSC_forceRAMSize=str(512 * 1024 * 1024))
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                                 f"http://127.0.0.1:{server.server_address[1]}/gc-churn.html", str(Path(directory) / "page.png")],
                                capture_output=True, text=True, timeout=180, env=environment)
    assert result.returncode == 0, result
    # release at load completion, a settle delay lets idle gc reclaim the arrays before this probe
    with tempfile.TemporaryDirectory(prefix="yozora-release-") as directory:
        released = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                                   f"http://127.0.0.1:{server.server_address[1]}/gc-garbage.html", str(Path(directory) / "page.png")],
                                  capture_output=True, text=True, timeout=120,
                                  env=dict(os.environ, YOZORA_RENDER_RELEASE="1", YOZORA_RENDER_SETTLE_MS="0"))
    server.shutdown()
    assert released.returncode == 0, released
    freed = re.search(r"memory release (\d+) kB -> (\d+) kB", released.stderr)
    assert freed and int(freed.group(1)) - int(freed.group(2)) > 25 * 1024, released.stderr
    assert re.search(r"done total=\d+", result.stderr) or "loaded and painted page" in result.stdout, result
    collections = result.stderr.count("GC END!")
    # the page keeps about 30 MB alive: a heap that may grow 20% over its live size collects 43 times, 1.5 needs about 21
    assert 8 <= collections <= 30, collections
    print(f"core gc: {collections} collections for the churn page, release gave back {(int(freed.group(1)) - int(freed.group(2))) // 1024} MB")


if __name__ == "__main__":
    main()
