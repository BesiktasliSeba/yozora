from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from urllib.parse import parse_qs, urlsplit

from test_core_loading import ROOT, read_png


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    seen = []

    def log_message(self, *arguments):
        pass

    def reply(self, body, status=200, headers=(), content_type="text/html; charset=utf-8"):
        body = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for key, value in headers:
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        Handler.seen.append((self.path, self.headers.get("Cookie", "")))
        path = urlsplit(self.path).path
        if path == "/redirect":
            self.reply("", 302, [("Location", "/widget"), ("Set-Cookie", "challenge=1; Path=/; HttpOnly")])
        elif path == "/widget":
            self.reply(f"""<!doctype html><title>child title</title>
<style>body{{margin:0;background:#0000ff}} input{{position:absolute;left:10px;margin:0}}
#check{{top:10px;width:30px;height:30px}} #text{{top:60px;width:180px;height:30px}}</style>
<input id=check type=checkbox><form action=/verify method=post><input id=text name=answer></form>
<script>
const origin = '{self.server.parent_origin}';
function report(value) {{ parent.postMessage(value, origin); }}
history.replaceState(null, '', '/widget#state');
if (document.cookie.includes('challenge=')) throw new Error('HttpOnly cookie exposed');
check.onclick = e => report('checked:' + check.checked + ':' + e.isTrusted);
text.oninput = () => report('text:' + text.value);
addEventListener('message', e => {{
    if (e.origin !== origin || e.source !== parent) return;
    if (e.ports.length) e.ports[0].postMessage('channel');
}});
parent.postMessage('wrong origin', 'http://invalid.example');
report('ready');
</script>""")
        elif path in ("/", "/challenge"):
            self.reply(f"""<!doctype html><title>outer</title><body style="margin:0;background:white">
<iframe id=widget name=challenge src="{self.server.child_origin}/redirect"
style="position:absolute;left:20px;top:40px;width:240px;height:200px;border:0"></iframe>
<script>
addEventListener('message', e => {{
    if (e.source !== widget.contentWindow || e.origin !== '{self.server.child_origin}') return;
    if (e.data === 'wrong origin') throw new Error('targetOrigin was ignored');
    if (e.data === 'ready') {{
        let isolated = false;
        try {{ void widget.contentWindow.document; }} catch (error) {{ isolated = error.name === 'SecurityError'; }}
        const channel = new MessageChannel();
        channel.port1.onmessage = event => {{ document.title = 'ready:' + (isolated && frames.challenge === widget.contentWindow) + ':' + event.data; }};
        widget.contentWindow.postMessage('port', e.origin, [channel.port2]);
    }} else document.title = e.data;
}});
</script>""", 403 if path == "/challenge" else 200)
        elif path == "/lifecycle":
            self.reply(f"""<!doctype html><title>outer</title><body>
<iframe id=widget src="{self.server.child_origin}/widget"></iframe>
<script>
addEventListener('message', e => {{
    if (e.source !== widget.contentWindow || e.origin !== '{self.server.child_origin}') return;
    widget.remove();
    const child = document.createElement('iframe');
    child.srcdoc = '<title>srcdoc title</title><script>parent.postMessage("removed-and-srcdoc", "*")<\\/script>';
    window.onmessage = event => {{ if (event.source === child.contentWindow) document.title = event.data; }};
    document.body.appendChild(child);
}});
</script>""")
        elif path == "/sandbox":
            self.reply(f"""<!doctype html><title>outer</title><body>
<iframe sandbox="allow-scripts" src="{self.server.child_origin}/sandbox-parent"></iframe>
<script>onmessage = e => {{ if (e.data === 'sandbox:true:null') document.title = e.data; }};</script>""")
        elif path == "/sandbox-parent":
            self.reply("""<!doctype html><title>sandbox parent</title>
<iframe src=/sandbox-child></iframe>""")
        elif path == "/sandbox-child":
            self.reply("""<!doctype html><title>sandbox child</title><script>
let denied = false;
try { void parent.document; } catch (error) { denied = error.name === 'SecurityError'; }
top.postMessage('sandbox:' + denied + ':' + self.origin, '*');
</script>""")
        elif path == "/blocked":
            query = parse_qs(urlsplit(self.path).query)
            source = "/denied" if "xfo" in query else "/csp-denied"
            self.reply(f"""<!doctype html><title>outer</title><body style='margin:0;background:white'>
<iframe src='{self.server.child_origin}{source}' style='border:0;width:240px;height:200px'></iframe>
<script>onmessage = () => document.title = 'policy failed';</script>""")
        elif path in ("/denied", "/csp-denied"):
            header = ("X-Frame-Options", "DENY") if path == "/denied" else ("Content-Security-Policy", "frame-ancestors 'none'")
            self.reply("<body style='background:red'><script>top.postMessage('forbidden','*')</script>", headers=[header])
        elif path == "/failed":
            self.reply("""<!doctype html><title>outer</title><body>
<iframe src='http://127.0.0.1:1/unreachable'></iframe>""")
        elif path == "/parent-csp":
            self.reply(f"""<!doctype html><title>outer</title><body>
<iframe src='{self.server.child_origin}/denied'></iframe>
<script>onmessage = () => document.title = 'policy failed';</script>""",
                       headers=[("Content-Security-Policy", "frame-src 'none'")])
        elif path == "/worker":
            self.reply("""<!doctype html><title>waiting</title><script>
const worker = new Worker('/worker.js');
worker.onerror = e => document.title = 'worker error:' + e.message;
worker.onmessage = e => { document.title = e.data; worker.terminate(); };
</script>""")
        elif path == "/worker.js":
            self.reply("""importScripts('/worker-lib.js');
postMessage('worker:' + workerValue + ':' + (typeof indexedDB === 'undefined'));""",
                       content_type="text/javascript")
        elif path == "/worker-lib.js":
            self.reply("const workerValue = 42;", content_type="text/javascript")
        elif path == "/blob-worker":
            self.reply("""<!doctype html><title>waiting</title><script>
const source = "importScripts('" + location.origin + "/worker-lib.js');postMessage('blob:' + workerValue + ':' + (typeof indexedDB === 'undefined'));";
const url = URL.createObjectURL(new Blob([source], {type:'text/javascript'}));
const worker = new Worker(url);
worker.onerror = e => document.title = 'worker error:' + e.message;
worker.onmessage = e => { document.title = e.data; worker.terminate(); URL.revokeObjectURL(url); };
</script>""")
        else:
            self.reply("missing", 404)

    def do_POST(self):
        body = self.rfile.read(int(self.headers["Content-Length"]))
        Handler.seen.append((self.path, self.headers.get("Cookie", ""), body))
        assert self.path == "/verify", self.path
        self.reply(f"""<!doctype html><title>verified child</title><script>
parent.postMessage('verified', '{self.server.parent_origin}');
</script>""")


def render(binary, url, actions="", title="outer", blue=False):
    with tempfile.TemporaryDirectory(prefix="yozora-frames-") as directory:
        output = Path(directory) / "page.png"
        environment = dict(os.environ)
        environment.pop("YOZORA_RENDER_ACTIONS", None)
        if actions:
            environment["YOZORA_RENDER_ACTIONS"] = actions
        result = subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"), url, str(output)],
                                capture_output=True, text=True, timeout=60,
                                env=environment)
        assert result.returncode == 0, result
        titles = [line.removeprefix("yozora-render: title ") for line in result.stderr.splitlines()
                  if line.startswith("yozora-render: title ")]
        assert titles[-1] == title, result.stderr
        assert f"yozora-render: navigation {url} back=0 forward=0\n" in result.stderr, result.stderr
        assert "targetOrigin was ignored" not in result.stderr, result.stderr
        assert "HttpOnly cookie exposed" not in result.stderr, result.stderr
        if blue:
            _, _, rows = read_png(output)
            assert rows[220][220 * 4:220 * 4 + 4] == bytes([0, 0, 255, 255]), result.stderr
    return result.stderr


def main():
    binary = str(Path(sys.argv[1]).resolve())
    parent = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    child = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    origin = f"http://127.0.0.1:{parent.server_port}"
    for server in (parent, child):
        server.parent_origin = origin
        server.child_origin = f"http://127.0.0.1:{child.server_port}"
        threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        render(binary, origin + "/", title="ready:true:channel", blue=True)
        render(binary, origin + "/challenge", title="ready:true:channel", blue=True)
        render(binary, origin + "/", "tap:45,65", "checked:true:true")
        log = render(binary, origin + "/", "tap:70,115;type:abcd;delete", "text:abc")
        assert "input kind 1" in log, log
        render(binary, origin + "/", "tap:70,115;type:answer\\n", "verified")
        assert any(path == "/verify" and "challenge=1" in cookie and body == b"answer=answer"
                   for path, cookie, *data in Handler.seen for body in data), Handler.seen
        render(binary, origin + "/lifecycle", title="removed-and-srcdoc")
        render(binary, origin + "/sandbox", title="sandbox:true:null")
        render(binary, origin + "/blocked?xfo=1")
        render(binary, origin + "/blocked?csp=1")
        render(binary, origin + "/failed")
        render(binary, origin + "/parent-csp")
        render(binary, origin + "/worker", title="worker:42:false")
        render(binary, origin + "/blob-worker", title="blob:42:false")
    finally:
        parent.shutdown()
        child.shutdown()
        parent.server_close()
        child.server_close()
    print("core frames: rendering, trusted click, text, POST, cookies, message ports, navigation isolation, removal, srcdoc, inherited sandbox, XFO, CSP and HTTP/blob workers with importScripts passed")


if __name__ == "__main__":
    main()
