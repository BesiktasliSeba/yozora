"""optional live probe using providers' public test sitekeys, never production challenges"""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import threading

ROOT = Path(__file__).resolve().parent.parent
PROVIDERS = {
    "turnstile": ("https://challenges.cloudflare.com/turnstile/v0/api.js", "cf-turnstile", "1x00000000000000000000AA"),
    "recaptcha": ("https://www.google.com/recaptcha/api.js", "g-recaptcha", "6LeIxAcTAAAAAJcZVRqyHh71UMIEGNQ_MXjiZKhI"),
    "hcaptcha": ("https://js.hcaptcha.com/1/api.js", "h-captcha", "10000000-ffff-ffff-ffff-000000000001"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("provider", choices=PROVIDERS)
    parser.add_argument("output", type=Path, help="directory for PNG and console log")
    args = parser.parse_args()
    source, css_class, sitekey = PROVIDERS[args.provider]
    page = f"""<!doctype html><meta charset=utf-8><title>waiting</title>
<body style="margin:0"><script>
function passed(token) {{ if (typeof token === 'string' && token.length) document.title = 'test-key-passed'; }}
function failed(code) {{ document.title = 'test-key-error:' + code; }}
onerror = function(message) {{ console.error(message); }};
</script><div class="{css_class}" data-sitekey="{sitekey}" data-callback="passed" data-error-callback="failed"></div>
<script src="{source}" async defer></script>""".encode()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *arguments):
            pass

        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(page)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(page)

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    args.output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, YOZORA_RENDER_SETTLE_MS="15000", YOZORA_LOG_REQUESTS="1")
    environment.pop("YOZORA_RENDER_ACTIONS", None)
    if args.provider != "turnstile":
        environment["YOZORA_RENDER_ACTIONS"] = "tap:30,30"
    try:
        result = subprocess.run([str(args.binary.resolve()), str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                                 f"http://127.0.0.1:{server.server_port}/", str(args.output / "page.png")],
                                capture_output=True, text=True, timeout=150, env=environment)
    finally:
        server.shutdown()
        server.server_close()
    log = args.output / "console.log"
    log.write_text(result.stdout + result.stderr)
    titles = [line.removeprefix("yozora-render: title ") for line in result.stderr.splitlines()
              if line.startswith("yozora-render: title ")]
    passed = result.returncode == 0 and bool(titles) and titles[-1] == "test-key-passed"
    print(f"{args.provider}: {'test key callback passed' if passed else 'FAILED'}, see {log}")
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
