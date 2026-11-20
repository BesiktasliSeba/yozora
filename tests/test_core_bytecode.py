from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent

# many small functions that all run: the stored file is much larger than the script and the second run decodes
# it instead of parsing
FUNCTIONS = 400
SCRIPT = "var out=[];\n" + "".join(
    f"function f{i}(a,b){{var s=0;for(var k=0;k<{3 + i % 5};k++){{s=(s+a*k+b+{i})|0;if(s%7==0)s^={i * 13 % 997};}}return s+(typeof a);}}\n"
    for i in range(FUNCTIONS)
) + "var total=0;for(var j=0;j<%d;j++){total+=String(eval('f'+j)(j,j+1)).length}\nconsole.log('total '+total);document.title='total '+total;\n" % FUNCTIONS
SMALL = "console.log('small script');"
PAGES = {
    "/big.js": ("application/javascript", SCRIPT.encode()),
    # the address changes on every visit, the script does not
    "/big-copy.js": ("application/javascript", SCRIPT.encode()),
    "/small.js": ("application/javascript", SMALL.encode()),
    "/page": ("text/html", b"<!doctype html><title>bytecode</title><script src='/big.js'></script><script src='/small.js'></script>"),
    "/copy": ("text/html", b"<!doctype html><title>bytecode</title><script src='/big-copy.js?visit=2'></script>"),
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *arguments):
        pass

    def do_GET(self):
        route = PAGES.get(self.path.split("?")[0])
        if not route:
            self.send_error(404)
            return
        kind, body = route
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)


def render(binary, cache, port, path, directory, extra=None):
    environment = dict(os.environ, YOZORA_RENDER_BYTECODE=str(cache))
    result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), f"http://127.0.0.1:{port}{path}", str(Path(directory) / "page.png")],
                            capture_output=True, text=True, timeout=90, env=environment)
    return result


def statistics(result):
    match = re.search(r"bytecode cache loaded (\d+) stale (\d+) stored (\d+) files, (\d+) bytes written", result.stderr)
    assert match, result.stderr
    return tuple(int(value) for value in match.groups())


def output(result):
    # what the script printed: the same text whether it was parsed or decoded
    return [line for line in result.stderr.splitlines() if line.startswith("console: log:")]


def main(binary):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory() as directory:
        cache = Path(directory) / "bytecode"
        cache.mkdir(mode=0o700)
        first = render(binary, cache, port, "/page", directory)
        assert first.returncode == 0, first
        loaded, stale, stored, written = statistics(first)
        files = sorted(cache.glob("*.bytecode"))
        # the script is stored, the small one is not worth a file
        assert (loaded, stale, stored) == (0, 0, 1) and len(files) == 1 and written == files[0].stat().st_size, (statistics(first), files)
        assert files[0].stat().st_size > len(SCRIPT), files[0].stat().st_size
        expected = output(first)
        assert any("total" in line for line in expected) and any("small script" in line for line in expected), expected
        assert not list(cache.glob("*.tmp"))

        # the second run maps the file and writes nothing
        second = render(binary, cache, port, "/page", directory)
        assert second.returncode == 0, second
        assert statistics(second) == (1, 0, 0, 0), statistics(second)
        assert output(second) == expected, (output(second), expected)

        # the same script under another address and query string finds the same file
        copy = render(binary, cache, port, "/copy", directory)
        assert copy.returncode == 0, copy
        assert statistics(copy)[0] == 1 and len(list(cache.glob("*.bytecode"))) == 1, (statistics(copy), list(cache.glob("*.bytecode")))
        assert any("total" in line for line in output(copy))

        # a file that is only a header fragment is ignored and the script runs from source
        stored_file = files[0]
        good = stored_file.read_bytes()
        stored_file.write_bytes(good[:30])
        third = render(binary, cache, port, "/page", directory)
        assert third.returncode == 0 and output(third) == expected, third
        assert statistics(third)[:1] == (0,), statistics(third)
        # the run that could not use it wrote a whole file in its place
        assert stored_file.stat().st_size > len(SCRIPT), stored_file.stat().st_size
        stored_file = next(iter(cache.glob("*.bytecode")))
        # a file that is long enough but not bytecode of this build is replaced as a whole
        stored_file.write_bytes(bytes(range(256)) * 8)
        fourth = render(binary, cache, port, "/page", directory)
        assert fourth.returncode == 0 and output(fourth) == expected, fourth
        loaded, stale, stored, _ = statistics(fourth)
        assert (loaded, stale, stored) == (1, 1, 1), statistics(fourth)
        assert stored_file.stat().st_size > len(SCRIPT)
        fifth = render(binary, cache, port, "/page", directory)
        assert output(fifth) == expected and statistics(fifth) == (1, 0, 0, 0), statistics(fifth)

        # a directory that others can read is refused, the scripts still run
        shared = Path(directory) / "shared"
        shared.mkdir(mode=0o755)
        shared.chmod(0o755)
        refused = render(binary, shared, port, "/page", directory)
        assert refused.returncode != 0 or "private" in refused.stderr, refused.stderr
        assert not list(shared.glob("*"))
    server.shutdown()
    print("bytecode cache: stores, maps, ignores damaged files and keeps one file for one script")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_core_bytecode.py yozora-render")
    main(sys.argv[1])
